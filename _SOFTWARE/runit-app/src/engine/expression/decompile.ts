import type { FormulaContext } from './formula'
import { parseFormulaState } from './formula'
import { OP_INFO, SYNTAX } from './syntax'

/*
 * RPN back to text: the bytecode is replayed on a stack of trees (dup repeats its value, swap and drop move trees) and the
 * tree is printed with the fewest brackets that read back as the same tree. The text uses the symbol of an operator where it
 * has one (`&&`, `%`), the function name otherwise.
 */

type Tree =
  | { readonly kind: 'num'; readonly value: number }
  | { readonly kind: 'in'; readonly pin: number }
  | { readonly kind: 'op'; readonly op: string; readonly args: readonly Tree[] }

export type DecompileResult = { readonly ok: true; readonly text: string } | { readonly ok: false; readonly reason: string }

/** The shortest text that reads back as the same float. */
const formatFloat = (value: number): string => {
  for (let digits = 1; digits <= 9; digits++) {
    const text = String(Number(value.toPrecision(digits)))
    if (Math.fround(Number(text)) === Math.fround(value)) return text
  }
  return String(value)
}

const formatBit = (value: number): string => (value > 0xff ? `0x${value.toString(16).toUpperCase()}` : String(value))

const isWord = (text: string): boolean => /^[\p{L}_]/u.test(text)

interface Printed {
  readonly text: string
  /** How tight it binds: a bracket is needed when it sits in a place that binds tighter. Infinity: an atom. */
  readonly level: number
}

/** The text of a formula whose code and constants are given (the bytes of a block's state, parsed). */
export const decompileCode = (context: FormulaContext, code: Uint8Array, constants: readonly number[]): DecompileResult => {
  const names = new Map([...context.opcodes].map(([name, value]) => [value, name]))
  const table = OP_INFO[context.flavor]
  const stack: Tree[] = []
  const fail = (reason: string): DecompileResult => ({ ok: false, reason })

  for (let pc = 0; pc < code.length; pc++) {
    const name = names.get(code[pc]!)
    const effect = name === undefined ? undefined : table[name]
    if (name === undefined || !effect) return fail(`Unknown opcode ${code[pc]} at byte ${pc}.`)
    if (name === 'END') break
    if (effect.arg !== 'none') {
      if (pc + 1 >= code.length) return fail(`${name} at byte ${pc} has no operand.`)
      const operand = code[++pc]!
      if (effect.arg === 'input') stack.push({ kind: 'in', pin: operand })
      else {
        const value = constants[operand]
        if (value === undefined) return fail(`Constant ${operand} does not exist.`)
        if (!Number.isFinite(value)) return fail(`Constant ${operand} is not a finite number.`)
        stack.push({ kind: 'num', value })
      }
      continue
    }
    if (stack.length < effect.pops) return fail(`${name} at byte ${pc} finds the stack short.`)
    if (name === 'DUP') stack.push(stack.at(-1)!)
    else if (name === 'DROP') stack.pop()
    else if (name === 'SWAP') stack.push(stack.splice(-2, 1)[0]!)
    else stack.push({ kind: 'op', op: name, args: stack.splice(-effect.pops) })
  }
  if (stack.length !== 1) return fail(`The code leaves ${stack.length} values; a formula leaves one.`)

  const syntax = SYNTAX[context.flavor]
  const top = syntax.binary.length // unary binds above every binary level, power above that
  const UNARY = top
  const POWER = top + 1
  const TERNARY = -1

  // the symbol a binary opcode is written with: a non-word one first (`&&`, `%`), else a word (`xor`, `mod`)
  const binaryOf = new Map<string, { symbol: string; level: number }>()
  const consider = (symbol: string, op: string, level: number) => {
    const have = binaryOf.get(op)
    if (!have || (isWord(have.symbol) && !isWord(symbol))) binaryOf.set(op, { symbol, level })
  }
  syntax.binary.forEach((entries, level) => entries.forEach(([symbol, op]) => consider(symbol, op, level)))
  if (syntax.power) consider(syntax.power[0], syntax.power[1], POWER)
  const unaryOf = new Map<string, string>()
  for (const [symbol, op] of Object.entries(syntax.unary)) if (!isWord(symbol) || !unaryOf.has(op)) unaryOf.set(op, symbol)

  // a function is written by its alias when that is a name, else its lowercase opcode name; either reads back
  const nameOf = (op: string): string => {
    const alias = context.aliases.get(op)
    return alias && context.functions.get(alias)?.op === op ? alias : op.toLowerCase()
  }

  const print = (tree: Tree): Printed => {
    if (tree.kind === 'in') return { text: `in${tree.pin}`, level: Infinity }
    if (tree.kind === 'num') {
      if (context.flavor === 'bit') return { text: formatBit(tree.value), level: Infinity }
      return { text: formatFloat(tree.value), level: tree.value < 0 || Object.is(tree.value, -0) ? UNARY : Infinity }
    }
    const binary = binaryOf.get(tree.op)
    if (binary && tree.args.length === 2) {
      const [a, b] = tree.args.map(print) as [Printed, Printed]
      const rightAssociative = binary.level === POWER
      // the exponent is read as a prefix expression: a unary operator needs no bracket there
      const left = rightAssociative ? a.level <= binary.level : a.level < binary.level
      const right = rightAssociative ? b.level < UNARY : b.level <= binary.level
      return { text: `${left ? `(${a.text})` : a.text} ${binary.symbol} ${right ? `(${b.text})` : b.text}`, level: binary.level }
    }
    const unary = unaryOf.get(tree.op)
    if (unary && tree.args.length === 1) {
      const a = print(tree.args[0]!)
      const bracket = a.level < UNARY || a.text.startsWith('-')
      return { text: `${unary}${isWord(unary) ? ' ' : ''}${bracket ? `(${a.text})` : a.text}`, level: UNARY }
    }
    if (syntax.ternary && tree.op === 'SEL') {
      const [c, t, f] = tree.args.map(print) as [Printed, Printed, Printed]
      return { text: `${c.level <= TERNARY ? `(${c.text})` : c.text} ? ${t.text} : ${f.text}`, level: TERNARY }
    }
    return { text: `${nameOf(tree.op)}(${tree.args.map((arg) => print(arg).text).join(', ')})`, level: Infinity }
  }
  return { ok: true, text: print(stack[0]!).text }
}

/** The formula in a block's state bytes, as text. */
export const decompileState = (context: FormulaContext, state: Uint8Array): DecompileResult => {
  const parsed = parseFormulaState(context.flavor, state)
  return parsed ? decompileCode(context, parsed.code, parsed.constants) : { ok: false, reason: 'The state is shorter than its header says.' }
}
