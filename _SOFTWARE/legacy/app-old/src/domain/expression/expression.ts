import type { VmBlockEncoding, VmOpcode } from '../descriptors'

/*
 * Infix formulas for the expression blocks (EXPR, EXPR_BIT) ⇄ their RPN
 * bytecode, driven by the block descriptor's opcode table:
 *
 *   (IN0 + 2) * IN1      min(max(IN0, 0), 100)      IN0 > IN1 ? IN0 : IN1
 *
 * - `IN<n>` reads input pin n; numbers become constants (one per distinct
 *   value, `const k`).
 * - Operators map onto opcodes by their descriptor alias, with C-like
 *   precedence; `c ? a : b` is `select`. EXPR: `^` is power, `%` is mod,
 *   `&&` / `||` / `!` the logic ops. EXPR_BIT: `^` is xor, `~` flips bits,
 *   numbers may be hex (0x…) or binary (0b…).
 * - Every other opcode whose alias is a word is a function with as many
 *   arguments as it pops: `min(a, b)`, `bit(a, 3)`, `recip(a)` for `1/x`.
 *
 * Errors carry the character range they are about, for the editor.
 */

export type ExpressionFlavor = 'float' | 'bits'

export interface ExpressionError {
  readonly message: string
  readonly start: number
  readonly end: number
}

export interface Token {
  readonly kind: 'number' | 'input' | 'name' | 'op' | 'open' | 'close' | 'comma' | 'question' | 'colon'
  readonly text: string
  readonly start: number
  readonly end: number
  /** number: its value; input: its pin. */
  readonly value?: number
}

type Node =
  | { readonly kind: 'number'; readonly value: number; readonly start: number; readonly end: number }
  | { readonly kind: 'input'; readonly pin: number; readonly start: number; readonly end: number }
  | { readonly kind: 'call'; readonly opcode: VmOpcode; readonly args: readonly Node[]; readonly start: number; readonly end: number }

export interface CompiledExpression {
  /** RPN tokens for ProgramBlock.expression.code: aliases, each operand a number after its opcode. */
  readonly code: readonly (string | number)[]
  readonly constants: readonly number[]
  /** Input pins it reads, ascending. */
  readonly inputs: readonly number[]
  /** Deepest the value stack gets. */
  readonly depth: number
  /** Bytes in the block state: 4 per constant + the code. */
  readonly bytes: number
}

export type ExpressionResult = { readonly ok: true; readonly compiled: CompiledExpression } | { readonly ok: false; readonly error: ExpressionError }

// ---------------------------------------------------------------------------
// Syntax tables
// ---------------------------------------------------------------------------

interface Binary {
  readonly symbol: string
  readonly alias: string
  readonly precedence: number
  readonly right?: boolean
}

const FLOAT_BINARY: readonly Binary[] = [
  { symbol: '^', alias: 'pow', precedence: 10, right: true },
  { symbol: '*', alias: '*', precedence: 9 },
  { symbol: '/', alias: '/', precedence: 9 },
  { symbol: '%', alias: 'mod', precedence: 9 },
  { symbol: '+', alias: '+', precedence: 8 },
  { symbol: '-', alias: '-', precedence: 8 },
  { symbol: '<', alias: '<', precedence: 6 },
  { symbol: '<=', alias: '<=', precedence: 6 },
  { symbol: '>', alias: '>', precedence: 6 },
  { symbol: '>=', alias: '>=', precedence: 6 },
  { symbol: '==', alias: '==', precedence: 5 },
  { symbol: '!=', alias: '!=', precedence: 5 },
  { symbol: '&&', alias: 'and', precedence: 3 },
  { symbol: 'and', alias: 'and', precedence: 3 },
  { symbol: 'xor', alias: 'xor', precedence: 2 },
  { symbol: '||', alias: 'or', precedence: 1 },
  { symbol: 'or', alias: 'or', precedence: 1 },
]

const BITS_BINARY: readonly Binary[] = [
  { symbol: '<<', alias: '<<', precedence: 7 },
  { symbol: '>>', alias: '>>', precedence: 7 },
  { symbol: '>>>', alias: '>>>', precedence: 7 },
  { symbol: '&', alias: '&', precedence: 4 },
  { symbol: '^', alias: '^', precedence: 3 },
  { symbol: '|', alias: '|', precedence: 2 },
]

const FLOAT_UNARY: Readonly<Record<string, string>> = { '-': 'neg', '!': 'not', not: 'not' }
const BITS_UNARY: Readonly<Record<string, string>> = { '~': '~' }

/** Aliases that aren't words, callable under another name. */
const FUNCTION_NAMES: Readonly<Record<string, string>> = { recip: '1/x' }
/** Stack plumbing: not part of the formula language (decompiled, never written). */
const PLUMBING = new Set(['end', 'in', 'const', 'dup', 'drop', 'swap'])
const TERNARY_PRECEDENCE = 0
const UNARY_PRECEDENCE = 9.5
const NAMED_CONSTANTS: Readonly<Record<ExpressionFlavor, Readonly<Record<string, number>>>> = {
  float: { pi: Math.PI, e: Math.E, true: 1, false: 0 },
  bits: { true: 1, false: 0 },
}

export interface ExpressionLanguage {
  readonly flavor: ExpressionFlavor
  readonly encoding: VmBlockEncoding
  readonly binary: readonly Binary[]
  readonly unary: Readonly<Record<string, string>>
  /** Function name → opcode. */
  readonly functions: ReadonlyMap<string, VmOpcode>
  readonly opcode: (alias: string) => VmOpcode | undefined
}

export const expressionLanguage = (encoding: VmBlockEncoding): ExpressionLanguage => {
  const flavor: ExpressionFlavor = encoding.constantType === 'f32' ? 'float' : 'bits'
  const byAlias = new Map(encoding.opcodes.map((opcode) => [opcode.alias, opcode]))
  const has = (alias: string) => byAlias.has(alias)
  const functions = new Map<string, VmOpcode>()
  for (const opcode of encoding.opcodes) {
    if (PLUMBING.has(opcode.alias) || opcode.pushes !== 1) continue
    if (/^[a-z][a-z0-9]*$/.test(opcode.alias)) functions.set(opcode.alias, opcode)
  }
  for (const [name, alias] of Object.entries(FUNCTION_NAMES)) if (byAlias.has(alias)) functions.set(name, byAlias.get(alias)!)
  const unary = Object.fromEntries(Object.entries(flavor === 'float' ? FLOAT_UNARY : BITS_UNARY).filter(([, alias]) => has(alias)))
  return {
    flavor,
    encoding,
    binary: (flavor === 'float' ? FLOAT_BINARY : BITS_BINARY).filter((entry) => has(entry.alias)),
    unary,
    functions,
    opcode: (alias) => byAlias.get(alias),
  }
}

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------

const SYMBOLS = ['>>>', '<<', '>>', '<=', '>=', '==', '!=', '&&', '||', '+', '-', '*', '/', '%', '^', '<', '>', '&', '|', '~', '!']

/** Split a formula into tokens; unknown characters are an error at their place. */
export const tokenize = (text: string, flavor: ExpressionFlavor): { tokens: Token[]; error?: ExpressionError } => {
  const tokens: Token[] = []
  let at = 0
  while (at < text.length) {
    const rest = text.slice(at)
    const space = /^\s+/.exec(rest)
    if (space) {
      at += space[0].length
      continue
    }
    const input = /^in\s*(\d+)/i.exec(rest)
    if (input && !/^in[a-z]/i.test(rest)) {
      tokens.push({ kind: 'input', text: input[0], start: at, end: at + input[0].length, value: Number(input[1]) })
      at += input[0].length
      continue
    }
    const number = flavor === 'bits' ? /^(0x[0-9a-f]+|0b[01]+|\d+)(?![\w.])/i.exec(rest) : /^(\d+\.?\d*(e[+-]?\d+)?|\.\d+(e[+-]?\d+)?)(?![\w.])/i.exec(rest)
    if (number) {
      const literal = number[0]
      const value = /^0b/i.test(literal) ? Number.parseInt(literal.slice(2), 2) : Number(literal)
      tokens.push({ kind: 'number', text: literal, start: at, end: at + literal.length, value })
      at += literal.length
      continue
    }
    const name = /^[a-z_][a-z0-9_]*/i.exec(rest)
    if (name) {
      tokens.push({ kind: 'name', text: name[0], start: at, end: at + name[0].length })
      at += name[0].length
      continue
    }
    const symbol = SYMBOLS.find((entry) => rest.startsWith(entry))
    if (symbol) {
      tokens.push({ kind: 'op', text: symbol, start: at, end: at + symbol.length })
      at += symbol.length
      continue
    }
    const single: Record<string, Token['kind']> = { '(': 'open', ')': 'close', ',': 'comma', '?': 'question', ':': 'colon' }
    const kind = single[rest[0]!]
    if (kind) {
      tokens.push({ kind, text: rest[0]!, start: at, end: at + 1 })
      at += 1
      continue
    }
    return { tokens, error: { message: `'${rest[0]}' is not part of a formula.`, start: at, end: at + 1 } }
  }
  return { tokens }
}

// ---------------------------------------------------------------------------
// Parse
// ---------------------------------------------------------------------------

class ParseError extends Error {
  readonly error: ExpressionError
  constructor(error: ExpressionError) {
    super(error.message)
    this.error = error
  }
}

const closest = (name: string, candidates: readonly string[]): string | undefined => {
  let best: string | undefined
  let bestScore = Infinity
  for (const candidate of candidates) {
    // Edit distance, small words only.
    const a = name.toLowerCase()
    const b = candidate
    const row = Array.from({ length: b.length + 1 }, (_, index) => index)
    for (let i = 1; i <= a.length; i++) {
      let previous = row[0]!
      row[0] = i
      for (let j = 1; j <= b.length; j++) {
        const current = row[j]!
        row[j] = Math.min(row[j]! + 1, row[j - 1]! + 1, previous + (a[i - 1] === b[j - 1] ? 0 : 1))
        previous = current
      }
    }
    const score = row[b.length]!
    if (score < bestScore) {
      bestScore = score
      best = candidate
    }
  }
  return bestScore <= Math.max(1, Math.floor(name.length / 3)) ? best : undefined
}

const parse = (tokens: readonly Token[], language: ExpressionLanguage, textLength: number): Node => {
  let at = 0
  const peek = () => tokens[at]
  const fail = (message: string, token?: Token): never => {
    throw new ParseError({ message, start: token?.start ?? textLength, end: token?.end ?? textLength })
  }
  const binaryOf = (token: Token | undefined): Binary | undefined =>
    token && (token.kind === 'op' || token.kind === 'name') ? language.binary.find((entry) => entry.symbol === token.text.toLowerCase()) : undefined
  const call = (alias: string, args: Node[], from: number, to: number): Node => {
    const opcode = language.opcode(alias)!
    return { kind: 'call', opcode, args, start: from, end: to }
  }

  const expression = (minimum: number): Node => {
    let left = unary()
    for (;;) {
      const token = peek()
      if (token?.kind === 'question' && minimum <= TERNARY_PRECEDENCE && language.opcode('select')) {
        at++
        const whenTrue = expression(TERNARY_PRECEDENCE)
        if (peek()?.kind !== 'colon') fail("'?' needs its ': else' part.", peek())
        at++
        const whenFalse = expression(TERNARY_PRECEDENCE)
        left = call('select', [left, whenTrue, whenFalse], left.start, whenFalse.end)
        continue
      }
      const binary = binaryOf(token)
      if (!binary || binary.precedence < minimum) return left
      at++
      const right = expression(binary.right ? binary.precedence : binary.precedence + 0.01)
      left = call(binary.alias, [left, right], left.start, right.end)
    }
  }

  const unary = (): Node => {
    const token = peek()
    if (token && (token.kind === 'op' || token.kind === 'name')) {
      const alias = language.unary[token.text.toLowerCase()]
      if (alias) {
        at++
        const operand = expression(UNARY_PRECEDENCE)
        // A minus on a number is a negative constant, not an operation.
        if (alias === 'neg' && operand.kind === 'number') return { ...operand, value: -operand.value, start: token.start }
        return call(alias, [operand], token.start, operand.end)
      }
      if (token.text === '+') {
        at++
        return expression(UNARY_PRECEDENCE)
      }
      if (token.text === '-') fail(`${language.flavor === 'bits' ? 'Bit expressions have no negative numbers' : "'-' needs a value after it"}.`, token)
    }
    return primary()
  }

  const primary = (): Node => {
    const token = peek()
    if (!token) return fail('The formula ends too early: a value is missing.')
    at++
    if (token.kind === 'number') return { kind: 'number', value: token.value!, start: token.start, end: token.end }
    if (token.kind === 'input') return { kind: 'input', pin: token.value!, start: token.start, end: token.end }
    if (token.kind === 'open') {
      const inner = expression(TERNARY_PRECEDENCE)
      if (peek()?.kind !== 'close') fail("A '(' is not closed.", tokens[at] ?? token)
      at++
      return inner
    }
    if (token.kind === 'name') {
      const name = token.text.toLowerCase()
      const named = NAMED_CONSTANTS[language.flavor][name]
      if (named !== undefined && peek()?.kind !== 'open') return { kind: 'number', value: named, start: token.start, end: token.end }
      const opcode = language.functions.get(name)
      if (!opcode) {
        const suggestion = closest(name, [...language.functions.keys(), ...Object.keys(NAMED_CONSTANTS[language.flavor])])
        return fail(`'${token.text}' is not a function or value${suggestion ? ` (did you mean ${suggestion}?)` : ''}.`, token)
      }
      if (peek()?.kind !== 'open') return fail(`${name} needs its argument${opcode.pops === 1 ? '' : 's'} in brackets: ${name}(${Array.from({ length: opcode.pops }, (_, index) => String.fromCharCode(97 + index)).join(', ')}).`, token)
      const open = tokens[at]!
      at++
      const args: Node[] = []
      if (peek()?.kind !== 'close') {
        for (;;) {
          args.push(expression(TERNARY_PRECEDENCE))
          if (peek()?.kind === 'comma') {
            at++
            continue
          }
          break
        }
      }
      const close = peek()
      if (close?.kind !== 'close') return fail(`${name}( is not closed.`, close ?? open)
      at++
      if (args.length !== opcode.pops) return fail(`${name} takes ${opcode.pops} argument${opcode.pops === 1 ? '' : 's'}, not ${args.length}.`, { ...token, end: close.end })
      return { kind: 'call', opcode, args, start: token.start, end: close.end }
    }
    if (token.kind === 'close') return fail("A ')' without its '('.", token)
    return fail(`A value is missing before '${token.text}'.`, token)
  }

  const root = expression(TERNARY_PRECEDENCE)
  const left = peek()
  if (left?.kind === 'op' && !binaryOf(left) && !language.unary[left.text]) {
    const operators = [...new Set(language.binary.map((entry) => entry.symbol))].filter((symbol) => !/^[a-z]/.test(symbol))
    fail(`'${left.text}' is not an operator of ${language.flavor === 'bits' ? 'bit' : 'number'} formulas (${[...operators, ...Object.keys(language.unary).filter((symbol) => !/^[a-z]/.test(symbol))].join(' ')}).`, left)
  }
  if (left) fail(left.kind === 'close' ? "A ')' without its '('." : left.kind === 'number' || left.kind === 'input' || left.kind === 'name' ? `An operator is missing before '${left.text}'.` : `'${left.text}' is out of place.`, left)
  return root
}

// ---------------------------------------------------------------------------
// Compile
// ---------------------------------------------------------------------------

const U32_MAX = 0xffffffff

/** Compile a formula to RPN; `maxInputs` is the block's input pin limit. */
export const compileExpression = (text: string, language: ExpressionLanguage, maxInputs: number): ExpressionResult => {
  const { tokens, error } = tokenize(text, language.flavor)
  if (error) return { ok: false, error }
  if (!tokens.length) return { ok: false, error: { message: 'The formula is empty.', start: 0, end: 0 } }
  let root: Node
  try {
    root = parse(tokens, language, text.length)
  } catch (cause) {
    if (cause instanceof ParseError) return { ok: false, error: cause.error }
    throw cause
  }

  const constants: number[] = []
  const code: (string | number)[] = []
  const inputs = new Set<number>()
  let depth = 0
  let deepest = 0
  const push = () => { depth++; deepest = Math.max(deepest, depth) }
  const inputOp = language.opcode('in')!
  const constOp = language.opcode('const')!
  let failure: ExpressionError | undefined

  const emit = (node: Node): void => {
    if (failure) return
    if (node.kind === 'input') {
      if (node.pin >= maxInputs) {
        failure = { message: `IN${node.pin}: the block has at most ${maxInputs} inputs (IN0..IN${maxInputs - 1}).`, start: node.start, end: node.end }
        return
      }
      inputs.add(node.pin)
      code.push(inputOp.alias, node.pin)
      push()
      return
    }
    if (node.kind === 'number') {
      let value = node.value
      if (language.flavor === 'bits') {
        if (!Number.isInteger(value) || value < 0 || value > U32_MAX) {
          failure = { message: `${value} is not a whole number 0..${U32_MAX} (0xFFFFFFFF).`, start: node.start, end: node.end }
          return
        }
      } else {
        value = Math.fround(value)
        if (!Number.isFinite(value)) {
          failure = { message: `${node.value} is too big for a float.`, start: node.start, end: node.end }
          return
        }
      }
      let index = constants.findIndex((entry) => Object.is(entry, value))
      if (index < 0) {
        index = constants.length
        constants.push(value)
      }
      if (index > 0xff) {
        failure = { message: 'More than 256 different numbers.', start: node.start, end: node.end }
        return
      }
      code.push(constOp.alias, index)
      push()
      return
    }
    for (const arg of node.args) emit(arg)
    code.push(node.opcode.alias)
    depth += node.opcode.pushes - node.opcode.pops
    deepest = Math.max(deepest, depth)
  }
  emit(root)
  if (failure) return { ok: false, error: failure }
  if (deepest > language.encoding.stackMax) return { ok: false, error: { message: `The formula needs ${deepest} values on the stack at once, the block holds ${language.encoding.stackMax}: split it, or nest less.`, start: 0, end: text.length } }
  const codeBytes = code.length
  return { ok: true, compiled: { code, constants, inputs: [...inputs].sort((a, b) => a - b), depth: deepest, bytes: constants.length * 4 + codeBytes } }
}

// ---------------------------------------------------------------------------
// Decompile
// ---------------------------------------------------------------------------

type Tree = { kind: 'number'; value: number } | { kind: 'input'; pin: number } | { kind: 'call'; alias: string; args: Tree[] }

const formatNumber = (value: number, flavor: ExpressionFlavor): string => {
  if (flavor === 'bits') return value > 0xff ? `0x${value.toString(16).toUpperCase()}` : String(value)
  // The shortest text that reads back as the same float.
  for (let digits = 1; digits <= 9; digits++) {
    const text = String(Number(value.toPrecision(digits)))
    if (Math.fround(Number(text)) === Math.fround(value)) return text
  }
  return String(value)
}

/**
 * RPN (ProgramBlock.expression) → formula text. Stack plumbing is resolved
 * (dup repeats its value). Undefined when the code is not a well-formed
 * expression (the editor shows it as RPN then).
 */
export const decompileExpression = (code: readonly (string | number)[], constants: readonly number[], language: ExpressionLanguage): string | undefined => {
  const stack: Tree[] = []
  for (let at = 0; at < code.length; at++) {
    const token = code[at]
    const opcode = typeof token === 'string' ? language.encoding.opcode(token) : undefined
    if (!opcode) return undefined
    if (opcode.alias === 'end') break
    if (opcode.operand !== 'none') {
      const operand = code[++at]
      if (typeof operand !== 'number') return undefined
      if (opcode.operand === 'input') stack.push({ kind: 'input', pin: operand })
      else {
        const value = constants[operand]
        if (value === undefined) return undefined
        stack.push({ kind: 'number', value })
      }
      continue
    }
    if (stack.length < opcode.pops) return undefined
    if (opcode.alias === 'dup') stack.push(stack.at(-1)!)
    else if (opcode.alias === 'drop') stack.pop()
    else if (opcode.alias === 'swap') stack.push(stack.splice(-2, 1)[0]!)
    else stack.push({ kind: 'call', alias: opcode.alias, args: stack.splice(-opcode.pops) })
  }
  if (stack.length !== 1) return undefined

  const symbolOf = new Map<string, Binary>()
  for (const entry of language.binary) if (!symbolOf.has(entry.alias) && !/^[a-z]/.test(entry.symbol)) symbolOf.set(entry.alias, entry)
  for (const entry of language.binary) if (!symbolOf.has(entry.alias)) symbolOf.set(entry.alias, entry)
  const unaryOf = new Map(Object.entries(language.unary).filter(([symbol]) => !/^[a-z]/.test(symbol)).map(([symbol, alias]) => [alias, symbol]))
  const nameOf = new Map([...language.functions].map(([name, opcode]) => [opcode.alias, name]))

  /** Text and the precedence it binds with (Infinity = atom). */
  const print = (tree: Tree): { text: string; precedence: number } => {
    if (tree.kind === 'input') return { text: `IN${tree.pin}`, precedence: Infinity }
    if (tree.kind === 'number') {
      const text = formatNumber(tree.value, language.flavor)
      return { text, precedence: tree.value < 0 ? UNARY_PRECEDENCE : Infinity }
    }
    const binary = symbolOf.get(tree.alias)
    if (binary && tree.args.length === 2) {
      const [a, b] = tree.args.map(print) as [{ text: string; precedence: number }, { text: string; precedence: number }]
      const leftNeeds = binary.right ? a.precedence <= binary.precedence : a.precedence < binary.precedence
      const rightNeeds = binary.right ? b.precedence < binary.precedence : b.precedence <= binary.precedence
      return { text: `${leftNeeds ? `(${a.text})` : a.text} ${binary.symbol} ${rightNeeds ? `(${b.text})` : b.text}`, precedence: binary.precedence }
    }
    const unarySymbol = unaryOf.get(tree.alias)
    if (unarySymbol && tree.args.length === 1) {
      const a = print(tree.args[0]!)
      return { text: `${unarySymbol}${a.precedence < UNARY_PRECEDENCE || a.text.startsWith('-') ? `(${a.text})` : a.text}`, precedence: UNARY_PRECEDENCE }
    }
    if (tree.alias === 'select' && tree.args.length === 3) {
      const [c, t, f] = tree.args.map(print) as [{ text: string; precedence: number }, { text: string; precedence: number }, { text: string; precedence: number }]
      return { text: `${c.precedence <= TERNARY_PRECEDENCE ? `(${c.text})` : c.text} ? ${t.text} : ${f.text}`, precedence: TERNARY_PRECEDENCE }
    }
    const name = nameOf.get(tree.alias) ?? tree.alias
    return { text: `${name}(${tree.args.map((arg) => print(arg).text).join(', ')})`, precedence: Infinity }
  }
  return print(stack[0]!).text
}
