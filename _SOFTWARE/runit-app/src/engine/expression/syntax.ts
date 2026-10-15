/*
 * The two expression blocks share one stack machine (`vm_expr_core.h`): a program is a list of opcodes, `IN` and `K` followed
 * by one operand byte (an input pin, a constant), everything else taking its operands from the stack, with no jumps. The
 * opcode numbers come from generated/common/enums.json (`vm_expr_op_e`, `vm_bit_op_e`); the stack effect of each is the opcode
 * table of vm_expr_core.h (`vm_expr_ops`, `vm_bit_ops`), kept below by name and held against the header by the tests.
 *
 *   float   a + b  a - b  a * b  a / b  a mod b  a ^ b (power)   comparisons   and or xor not   c ? t : f   functions
 *   bit     a | b  a ^ b  a & b  a << b  a >> b  a >>> b  ~a   functions (rol, ror, bit, setbit, clearbit, togglebit, ...)
 *   both    inN reads input N; numbers; true and false; pi and e (float); every function by its alias or its opcode name
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

/** The stack plumbing: in the bytecode, never written in a formula. */
export const PLUMBING: ReadonlySet<string> = new Set(['END', 'IN', 'K', 'DUP', 'DROP', 'SWAP'])

export interface Syntax {
  /** Binary operators by precedence, low to high: [symbol, opcode name]. Words (`or`, `mod`) are operators too. */
  readonly binary: readonly (readonly (readonly [string, string])[])[]
  readonly unary: Readonly<Record<string, string>>
  /** The power operator, binding tighter than a unary one (float only). */
  readonly power?: readonly [string, string]
  /** Names a function can also go by, besides its alias and opcode name (the C `@format`): `setbit`, `popcnt`. */
  readonly names: Readonly<Record<string, string>>
  /** Names that stand for a number. */
  readonly constants: Readonly<Record<string, number>>
  /** Whether `c ? a : b` is the SEL operation. */
  readonly ternary: boolean
}

export const SYNTAX: Readonly<Record<Flavor, Syntax>> = {
  float: {
    binary: [
      [['or', 'OR'], ['||', 'OR']],
      [['xor', 'XOR']],
      [['and', 'AND'], ['&&', 'AND']],
      [['==', 'EQ'], ['!=', 'NE']],
      [['<', 'LT'], ['<=', 'LE'], ['>', 'GT'], ['>=', 'GE']],
      [['+', 'ADD'], ['-', 'SUB']],
      [['*', 'MUL'], ['/', 'DIV'], ['mod', 'MOD'], ['%', 'MOD']],
    ],
    unary: { '-': 'NEG', '!': 'NOT', not: 'NOT' },
    power: ['^', 'POW'],
    names: { recip: 'RECIP', sq: 'SQUARE', square: 'SQUARE', ln: 'LOG', log: 'LOG', select: 'SEL' },
    constants: { pi: Math.fround(Math.PI), e: Math.fround(Math.E), true: 1, false: 0 },
    ternary: true,
  },
  bit: {
    binary: [[['|', 'OR']], [['^', 'XOR']], [['&', 'AND']], [['<<', 'SHL'], ['>>', 'SHR'], ['>>>', 'SAR']]],
    unary: { '~': 'NOT' },
    names: { setbit: 'SET', clearbit: 'CLR', togglebit: 'TGL', popcnt: 'POPCNT', popcount: 'POPCNT', set: 'SET', clear: 'CLR', toggle: 'TGL', bit: 'GET' },
    constants: { true: 1, false: 0 },
    ternary: false,
  },
}

export const SYMBOLS = ['>>>', '<<', '>>', '<=', '>=', '==', '!=', '&&', '||', '+', '-', '*', '/', '%', '^', '(', ')', ',', '?', ':', '<', '>', '&', '|', '~', '!']

const isName = (text: string): boolean => /^[\p{L}_][\p{L}\p{N}_]*$/u.test(text)

export interface FunctionEntry {
  readonly op: string
  readonly arity: number
}

/**
 * Every opcode that takes operands from the stack can be called by name: its alias and its opcode name, plus the C format
 * names. `aliases` is opcode name -> alias from the generated enum.
 */
export const functionTable = (flavor: Flavor, aliases: ReadonlyMap<string, string>): ReadonlyMap<string, FunctionEntry> => {
  const functions = new Map<string, FunctionEntry>()
  const table = OP_INFO[flavor]
  const add = (name: string, op: string) => {
    const entry = table[op]
    if (entry && entry.arg === 'none' && entry.pops > 0 && !PLUMBING.has(op) && isName(name)) functions.set(name.toLowerCase(), { op, arity: entry.pops })
  }
  for (const op of Object.keys(table)) add(op.toLowerCase(), op)
  for (const [op, alias] of aliases) add(alias, op)
  for (const [name, op] of Object.entries(SYNTAX[flavor].names)) add(name, op)
  return functions
}
