import { type Flavor, type FunctionEntry, SYMBOLS, SYNTAX } from './syntax'

/* ---- errors ---- */

export interface FormulaError {
  readonly message: string
  /** The characters of the formula it is about: [from, to). */
  readonly from: number
  readonly to: number
}

export class Failure extends Error {
  constructor(readonly error: FormulaError) {
    super(error.message)
  }
}

/* ---- lexing ---- */

interface RawToken {
  readonly kind: 'number' | 'word' | 'symbol' | 'end'
  readonly text: string
  readonly from: number
  readonly to: number
}

const lex = (text: string, flavor: Flavor): { tokens: RawToken[]; error?: FormulaError } => {
  const tokens: RawToken[] = []
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
    if (!symbol) return { tokens, error: { message: `Unexpected '${rest[0]}'.`, from: i, to: i + 1 } }
    tokens.push({ kind: 'symbol', text: symbol, from: i, to: i + symbol.length })
    i += symbol.length
  }
  tokens.push({ kind: 'end', text: '', from: text.length, to: text.length })
  return { tokens }
}

/** What an editor colours a formula by. */
export interface Token {
  readonly kind: 'number' | 'input' | 'name' | 'op' | 'open' | 'close' | 'comma' | 'question' | 'colon'
  readonly text: string
  readonly from: number
  readonly to: number
}

const isOperatorWord = (flavor: Flavor, word: string): boolean => SYNTAX[flavor].binary.flat().some(([s]) => s === word) || Object.hasOwn(SYNTAX[flavor].unary, word)

const PUNCTUATION: Readonly<Record<string, Token['kind']>> = { '(': 'open', ')': 'close', ',': 'comma', '?': 'question', ':': 'colon' }

/** The tokens of a formula, as far as it can be read; a character that belongs to no token is the error, at its place. */
export const tokenize = (text: string, flavor: Flavor): { readonly tokens: readonly Token[]; readonly error?: FormulaError } => {
  const { tokens, error } = lex(text, flavor)
  const classified = tokens
    .filter((t) => t.kind !== 'end')
    .map((t): Token => {
      if (t.kind === 'number') return { ...t, kind: 'number' }
      if (t.kind === 'word') return { ...t, kind: /^in\d+$/i.test(t.text) ? 'input' : isOperatorWord(flavor, t.text.toLowerCase()) ? 'op' : 'name' }
      return { ...t, kind: PUNCTUATION[t.text] ?? 'op' }
    })
  return error ? { tokens: classified, error } : { tokens: classified }
}

/* ---- parsing: a tree of what the stack machine will do ---- */

export type Node =
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

export const parse = (text: string, flavor: Flavor, functions: ReadonlyMap<string, FunctionEntry>): Node => {
  const lexed = lex(text, flavor)
  if (lexed.error) throw new Failure(lexed.error)
  const tokens = lexed.tokens
  const syntax = SYNTAX[flavor]
  let at = 0
  const peek = () => tokens[at]!
  const next = () => tokens[at++]!
  const fail = (message: string, t: RawToken = peek()): never => {
    throw new Failure({ message, from: t.from, to: Math.max(t.to, t.from + 1) })
  }
  const isSymbol = (t: RawToken, s: string) => t.kind === 'symbol' && t.text === s
  const operatorOf = (t: RawToken): { level: number; op: string } | undefined => {
    if (t.kind !== 'symbol' && t.kind !== 'word') return undefined
    const word = t.kind === 'word' ? t.text.toLowerCase() : t.text
    for (let level = 0; level < syntax.binary.length; level++) {
      const hit = syntax.binary[level]!.find(([symbol]) => symbol === word)
      if (hit) return { level, op: hit[1] }
    }
    return undefined
  }

  const number = (t: RawToken): Node => {
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
      if (Object.hasOwn(syntax.constants, lower)) return { kind: 'num', value: syntax.constants[lower]!, from: t.from, to: t.to }
      const fn = functions.get(lower)
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
      const close = [...functions.keys(), ...Object.keys(syntax.constants)].map((name) => [name, edit(lower, name)] as const).sort((a, b) => a[1] - b[1])[0]
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
      if (syntax.ternary && min === 0 && isSymbol(t, '?')) {
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
