import type { EnumTables } from '../generated/wire'
import type { BlockCodec } from './codecs'

/*
 * Formulas: the text a user writes in an expression block, as the bytecode the board runs. The two expression blocks share one
 * stack machine (`vm_expr_core.h`): a program is a list of opcodes, `IN` and `K` followed by one operand byte (an input pin, a
 * constant), everything else taking its operands from the stack, with no jumps. The state is
 *
 *   u8 const_cnt, u8 rt (0), u16 code_len, const_cnt x 4 bytes (a float, or a u32 for the bit block), code_len bytes of code
 *
 * and the board checks it once at load: every opcode known, every operand in range and an input wired, the stack never short
 * or over 16, ending with exactly one value. The opcode numbers come from generated/common/enums.json (`vm_expr_op_e`,
 * `vm_bit_op_e`); the stack effect of each is the opcode table of vm_expr_core.h (`vm_expr_ops`, `vm_bit_ops`), kept below by
 * name and held against the header by the tests.
 *
 *   float   a + b  a - b  a * b  a / b  a mod b  a ^ b (power)   comparisons   and or xor not   c ? t : f   functions
 *   bit     a | b  a ^ b  a & b  a << b  a >> b  a >>> b  ~a   functions (rol, ror, bit, setbit, clearbit, togglebit, ...)
 *   both    inN reads input N; numbers; pi and e (float); every function by its alias or its opcode name, with its operands
 */

export type Flavor = 'float' | 'bit'

/** `VM_EXPR_STACK_MAX`. */
export const STACK_MAX = 16

export interface OpInfo {
  readonly pops: number
  readonly pushes: number
  readonly arg: 'none' | 'input' | 'const'
}

const info = (pops: number, pushes: number, arg: OpInfo['arg'] = 'none'): OpInfo => ({ pops, pushes, arg })

/** The opcode table of vm_expr_core.h, by opcode name without its prefix (`VM_EXPR_` / `VM_BIT_`). */
export const OP_INFO: Readonly<Record<Flavor, Readonly<Record<string, OpInfo>>>> = (() => {
  const common = { END: info(0, 0), IN: info(0, 1, 'input'), K: info(0, 1, 'const'), DUP: info(1, 2), DROP: info(1, 0), SWAP: info(2, 2) }
  const table = (unary: string, binary: string, ternary = ''): Record<string, OpInfo> => ({
    ...common,
    ...Object.fromEntries(unary.split(' ').filter(Boolean).map((n) => [n, info(1, 1)])),
    ...Object.fromEntries(binary.split(' ').filter(Boolean).map((n) => [n, info(2, 1)])),
    ...Object.fromEntries(ternary.split(' ').filter(Boolean).map((n) => [n, info(3, 1)])),
  })
  return {
    float: table(
      'NEG ABS SQRT SQUARE RECIP FLOOR CEIL ROUND TRUNC SIGN SIN COS TAN ASIN ACOS ATAN EXP LOG LOG10 LOG2 DEG RAD NOT',
      'ADD SUB MUL DIV MOD POW ROOT MIN MAX ATAN2 HYPOT LT LE GT GE EQ NE AND OR XOR',
      'SEL',
    ),
    bit: table('NOT POPCNT CLZ CTZ BSWAP', 'AND OR XOR SHL SHR SAR ROL ROR GET SET CLR TGL'),
  }
})()

interface Syntax {
  /** Binary operators by precedence, low to high: [symbol, opcode name]. Words (`or`, `mod`) are operators too. */
  readonly binary: readonly (readonly (readonly [string, string])[])[]
  readonly unary: Readonly<Record<string, string>>
  /** The power operator, binding tighter than a unary one (float only). */
  readonly power?: readonly [string, string]
  /** Names a function can also go by, besides its alias and opcode name (the C `@format`): `setbit`, `popcnt`. */
  readonly names: Readonly<Record<string, string>>
}

const SYNTAX: Readonly<Record<Flavor, Syntax>> = {
  float: {
    binary: [
      [['or', 'OR']],
      [['xor', 'XOR']],
      [['and', 'AND']],
      [['==', 'EQ'], ['!=', 'NE']],
      [['<', 'LT'], ['<=', 'LE'], ['>', 'GT'], ['>=', 'GE']],
      [['+', 'ADD'], ['-', 'SUB']],
      [['*', 'MUL'], ['/', 'DIV'], ['mod', 'MOD'], ['%', 'MOD']],
    ],
    unary: { '-': 'NEG', not: 'NOT' },
    power: ['^', 'POW'],
    names: { recip: 'RECIP', sq: 'SQUARE', square: 'SQUARE', ln: 'LOG', log: 'LOG', select: 'SEL' },
  },
  bit: {
    binary: [[['|', 'OR']], [['^', 'XOR']], [['&', 'AND']], [['<<', 'SHL'], ['>>', 'SHR'], ['>>>', 'SAR']]],
    unary: { '~': 'NOT' },
    names: { setbit: 'SET', clearbit: 'CLR', togglebit: 'TGL', popcnt: 'POPCNT', popcount: 'POPCNT', set: 'SET', clear: 'CLR', toggle: 'TGL', bit: 'GET' },
  },
}

/* ---- lexing ---- */

interface Token {
  readonly kind: 'number' | 'word' | 'symbol' | 'end'
  readonly text: string
  readonly from: number
  readonly to: number
}

export interface FormulaError {
  readonly message: string
  readonly from: number
  readonly to: number
}

class Failure extends Error {
  constructor(readonly error: FormulaError) {
    super(error.message)
  }
}

const SYMBOLS = ['>>>', '<<', '>>', '<=', '>=', '==', '!=', '+', '-', '*', '/', '%', '^', '(', ')', ',', '?', ':', '<', '>', '&', '|', '~']

const lex = (text: string, flavor: Flavor): Token[] => {
  const tokens: Token[] = []
  let i = 0
  while (i < text.length) {
    const rest = text.slice(i)
    const space = /^\s+/.exec(rest)
    if (space) {
      i += space[0].length
      continue
    }
    const number = flavor === 'bit' ? /^(0x[0-9a-f]+|0b[01]+|\d+)/i.exec(rest) : /^(\d+\.?\d*(e[+-]?\d+)?|\.\d+(e[+-]?\d+)?)/i.exec(rest)
    if (number) {
      tokens.push({ kind: 'number', text: number[0], from: i, to: i + number[0].length })
      i += number[0].length
      continue
    }
    const word = /^[\p{L}_][\p{L}\p{N}_]*/u.exec(rest)
    if (word) {
      tokens.push({ kind: 'word', text: word[0], from: i, to: i + word[0].length })
      i += word[0].length
      continue
    }
    const symbol = SYMBOLS.find((s) => rest.startsWith(s))
    if (!symbol) throw new Failure({ message: `Unexpected '${rest[0]}'.`, from: i, to: i + 1 })
    tokens.push({ kind: 'symbol', text: symbol, from: i, to: i + symbol.length })
    i += symbol.length
  }
  tokens.push({ kind: 'end', text: '', from: text.length, to: text.length })
  return tokens
}

/* ---- parsing: a tree of what the stack machine will do ---- */

type Node =
  | { readonly kind: 'num'; readonly value: number; readonly from: number; readonly to: number }
  | { readonly kind: 'in'; readonly pin: number; readonly from: number; readonly to: number }
  | { readonly kind: 'op'; readonly op: string; readonly args: readonly Node[]; readonly from: number; readonly to: number }

const edit = (a: string, b: string): number => {
  const row = Array.from({ length: b.length + 1 }, (_, j) => j)
  for (let i = 1; i <= a.length; i++) {
    let prev = row[0]!
    row[0] = i
    for (let j = 1; j <= b.length; j++) {
      const keep = row[j]!
      row[j] = Math.min(row[j]! + 1, row[j - 1]! + 1, prev + (a[i - 1] === b[j - 1] ? 0 : 1))
      prev = keep
    }
  }
  return row[b.length]!
}

interface Dictionary {
  /** Function name -> opcode name, with the operand count it takes. */
  readonly functions: ReadonlyMap<string, { readonly op: string; readonly arity: number }>
}

/** Every opcode that takes operands from the stack can be called by name: its alias and its opcode name, plus the C format names. */
const dictionary = (flavor: Flavor, aliases: ReadonlyMap<string, string>): Dictionary => {
  const functions = new Map<string, { op: string; arity: number }>()
  const table = OP_INFO[flavor]
  const add = (name: string, op: string) => {
    const entry = table[op]
    if (entry && entry.arg === 'none' && entry.pops > 0 && !['DUP', 'DROP', 'SWAP'].includes(op) && /^[\p{L}_][\p{L}\p{N}_]*$/u.test(name)) functions.set(name.toLowerCase(), { op, arity: entry.pops })
  }
  for (const op of Object.keys(table)) add(op.toLowerCase(), op)
  for (const [op, alias] of aliases) add(alias, op)
  for (const [name, op] of Object.entries(SYNTAX[flavor].names)) add(name, op)
  return { functions }
}

const parse = (text: string, flavor: Flavor, aliases: ReadonlyMap<string, string>): Node => {
  const tokens = lex(text, flavor)
  const syntax = SYNTAX[flavor]
  const dict = dictionary(flavor, aliases)
  let at = 0
  const peek = () => tokens[at]!
  const next = () => tokens[at++]!
  const fail = (message: string, t: Token = peek()): never => {
    throw new Failure({ message, from: t.from, to: Math.max(t.to, t.from + 1) })
  }
  const isSymbol = (t: Token, s: string) => t.kind === 'symbol' && t.text === s
  const operatorOf = (t: Token): { level: number; op: string } | undefined => {
    if (t.kind !== 'symbol' && t.kind !== 'word') return undefined
    const word = t.kind === 'word' ? t.text.toLowerCase() : t.text
    for (let level = 0; level < syntax.binary.length; level++) {
      const hit = syntax.binary[level]!.find(([symbol]) => symbol === word)
      if (hit) return { level, op: hit[1] }
    }
    return undefined
  }

  const number = (t: Token): Node => {
    const value = flavor === 'bit' ? Number(t.text.toLowerCase().startsWith('0b') ? `0b${t.text.slice(2)}` : t.text) : Number(t.text)
    if (!Number.isFinite(value)) fail(`'${t.text}' is not a number.`, t)
    if (flavor === 'bit' && (!Number.isInteger(value) || value < 0 || value > 0xffffffff)) fail(`'${t.text}' does not fit 32 bits.`, t)
    return { kind: 'num', value: flavor === 'float' ? Math.fround(value) : value, from: t.from, to: t.to }
  }

  const primary = (): Node => {
    const t = next()
    if (t.kind === 'number') return number(t)
    if (isSymbol(t, '(')) {
      const inner = expression(0)
      if (!isSymbol(peek(), ')')) fail("A ')' is expected.")
      next()
      return inner
    }
    if (t.kind === 'word') {
      const lower = t.text.toLowerCase()
      const pin = /^in(\d+)$/.exec(lower)
      if (pin) return { kind: 'in', pin: Number(pin[1]), from: t.from, to: t.to }
      if (flavor === 'float' && (lower === 'pi' || lower === 'e')) return { kind: 'num', value: Math.fround(lower === 'pi' ? Math.PI : Math.E), from: t.from, to: t.to }
      const fn = dict.functions.get(lower)
      if (fn && isSymbol(peek(), '(')) {
        next()
        const args: Node[] = []
        if (!isSymbol(peek(), ')')) {
          do args.push(expression(0))
          while (isSymbol(peek(), ',') && next())
        }
        const close = peek()
        if (!isSymbol(close, ')')) fail("A ')' is expected.")
        next()
        if (args.length !== fn.arity) fail(`${t.text} takes ${fn.arity} value${fn.arity === 1 ? '' : 's'}, not ${args.length}.`, t)
        return { kind: 'op', op: fn.op, args, from: t.from, to: close.to }
      }
      if (fn) fail(`'${t.text}' is a function: write ${t.text}(…).`, t)
      const close = [...dict.functions.keys(), 'pi', 'e'].map((name) => [name, edit(lower, name)] as const).sort((a, b) => a[1] - b[1])[0]
      fail(`Unknown name '${t.text}'.${close && close[1] <= 2 ? ` Did you mean '${close[0]}'?` : ''} Inputs are in0, in1, ...`, t)
    }
    return fail(t.kind === 'end' ? 'The formula ends too soon.' : `Unexpected '${t.text}'.`, t)
  }

  const prefix = (): Node => {
    const t = peek()
    const key = t.kind === 'word' ? t.text.toLowerCase() : t.text
    const op = (t.kind === 'symbol' || t.kind === 'word') && Object.hasOwn(syntax.unary, key) ? syntax.unary[key] : undefined
    if (op) {
      next()
      const operand = prefix()
      // a minus in front of a number is the number: one constant, not a constant and an operation
      if (op === 'NEG' && operand.kind === 'num') return { kind: 'num', value: Math.fround(-operand.value), from: t.from, to: operand.to }
      return { kind: 'op', op, args: [operand], from: t.from, to: operand.to }
    }
    return power()
  }

  const power = (): Node => {
    const base = primary()
    if (syntax.power && isSymbol(peek(), syntax.power[0])) {
      next()
      const exponent = prefix() // right-associative, and 2^-1 reads as 2^(-1)
      return { kind: 'op', op: syntax.power[1], args: [base, exponent], from: base.from, to: exponent.to }
    }
    return base
  }

  const expression = (min: number): Node => {
    let left = prefix()
    for (;;) {
      const t = peek()
      if (flavor === 'float' && min === 0 && isSymbol(t, '?')) {
        next()
        const yes = expression(0)
        if (!isSymbol(peek(), ':')) fail("A ':' is expected after the first choice.")
        next()
        const no = expression(0)
        left = { kind: 'op', op: 'SEL', args: [left, yes, no], from: left.from, to: no.to }
        continue
      }
      const operator = operatorOf(t)
      if (!operator || operator.level < min) break
      next()
      const right = expression(operator.level + 1)
      left = { kind: 'op', op: operator.op, args: [left, right], from: left.from, to: right.to }
    }
    return left
  }

  const tree = expression(0)
  if (peek().kind !== 'end') fail(`Unexpected '${peek().text}'.`)
  return tree
}

/* ---- emitting ---- */

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

export interface FormulaContext {
  readonly flavor: Flavor
  /** Opcode name (without prefix) -> number, from the generated enum. */
  readonly opcodes: ReadonlyMap<string, number>
  /** Opcode name -> alias, from the generated enum (`+`, `mod`, `ln`): the words a formula may call a function by. */
  readonly aliases: ReadonlyMap<string, string>
}

/** Compiles a formula. `inputCount` is how many inputs the block has; `wired(i)` says whether input i has a wire. */
export const compileFormula = (context: FormulaContext, text: string, inputCount: number, wired: (pin: number) => boolean): FormulaResult => {
  try {
    if (text.trim() === '') throw new Failure({ message: 'The formula is empty.', from: 0, to: 0 })
    const tree = parse(text, context.flavor, context.aliases)
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

/* ---- the codecs ---- */

const prefixOf = (flavor: Flavor): string => (flavor === 'float' ? 'VM_EXPR_' : 'VM_BIT_')
const enumOf = (flavor: Flavor): string => (flavor === 'float' ? 'vm_expr_op_e' : 'vm_bit_op_e')

/** What a formula needs from the generated enum of its opcodes. */
export const formulaContext = (flavor: Flavor, enums: EnumTables): FormulaContext => {
  const table = enums[enumOf(flavor)]
  if (!table) throw new Error(`generated/common/enums.json has no enum '${enumOf(flavor)}'.`)
  const opcodes = new Map<string, number>()
  const aliases = new Map<string, string>()
  for (const member of table.members) {
    if (!member.name.startsWith(prefixOf(flavor)) || member.name.endsWith('_OP_CNT')) continue
    const name = member.name.slice(prefixOf(flavor).length)
    opcodes.set(name, member.value)
    if (member.alias) aliases.set(name, member.alias.toLowerCase())
  }
  return { flavor, opcodes, aliases }
}

/**
 * The codecs of the two expression blocks: `expression` (floats) and `bit-expression` (u32). What the editor leaves on the
 * block is its text: `instance.editor = { text: 'in0 * 2 + in1' }`.
 */
export const expressionCodecs = (enums: EnumTables): readonly BlockCodec[] =>
  (['float', 'bit'] as const).map((flavor): BlockCodec => {
    const context = formulaContext(flavor, enums)
    return {
      editor: flavor === 'float' ? 'expression' : 'bit-expression',
      encode: ({ instance, pins, report }) => {
        const text = (instance.editor as { text?: unknown } | undefined)?.text
        if (typeof text !== 'string') {
          report('error', 'expression-empty', 'The block has no formula.')
          return null
        }
        const result = compileFormula(context, text, pins.inputs.length, (pin) => instance.inputs[pin] != null)
        if (!result.ok) {
          report('error', 'expression-error', `${result.error.message} (characters ${result.error.from + 1}-${result.error.to})`)
          return null
        }
        return formulaState(flavor, result.formula)
      },
    }
  })
