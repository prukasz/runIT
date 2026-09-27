import { describe, expect, it } from 'vitest'
import { runitVmCatalog } from '../descriptors'
import { compileExpression, decompileExpression, expressionLanguage, tokenize } from '.'

const catalog = runitVmCatalog()
const float = expressionLanguage(catalog.block('EXPR')!.encoding!)
const bits = expressionLanguage(catalog.block('EXPR_BIT')!.encoding!)

const ok = (text: string, language = float) => {
  const result = compileExpression(text, language, 16)
  if (!result.ok) throw new Error(`${text}: ${result.error.message}`)
  return result.compiled
}
const error = (text: string, language = float) => {
  const result = compileExpression(text, language, 16)
  if (result.ok) throw new Error(`${text} compiled`)
  return result.error
}
/** RPN as the descriptor examples write it: VM_EXPR_IN, 0, … without END. */
const symbols = (code: readonly (string | number)[], language = float) => code.map((token) => (typeof token === 'number' ? String(token) : language.encoding.opcode(token)!.symbol))

describe('formulas → RPN', () => {
  it.each([
    ['EXPR', '(a + 2) * b', '(IN0 + 2) * IN1'],
    ['EXPR', 'larger of a and b', 'IN0 > IN1 ? IN0 : IN1'],
    ['EXPR', 'clamp a to 0..100', 'min(max(IN0, 0), 100)'],
    ['EXPR', 'length of (a, b)', 'hypot(IN0, IN1)'],
    ['EXPR_BIT', 'bit 3 of a', 'bit(IN0, 3)'],
    ['EXPR_BIT', '(a << 4) | b', '(IN0 << 4) | IN1'],
    ['EXPR_BIT', 'set bits in a', 'popcount(IN0)'],
  ])('%s example "%s" as %s', (key, title, formula) => {
    const language = key === 'EXPR' ? float : bits
    const example = catalog.block(key)!.encoding!.examples.find((entry) => entry.title === title)!
    const compiled = ok(formula, language)
    expect(symbols(compiled.code, language)).toEqual(example.code.filter((token) => !token.endsWith('_END')))
    expect(compiled.constants).toEqual(example.constants)
  })

  it('makes each distinct number one constant', () => {
    const compiled = ok('IN0 * 2 + IN1 * 2 - 0.5')
    expect(compiled.constants).toEqual([2, 0.5])
    expect(compiled.code).toEqual(['in', 0, 'const', 0, '*', 'in', 1, 'const', 0, '*', '+', 'const', 1, '-'])
    expect(compiled.inputs).toEqual([0, 1])
    expect(compiled.bytes).toBe(2 * 4 + 14)
  })

  it('follows precedence: power binds tighter than minus, logic loosest', () => {
    expect(ok('-IN0 ^ 2').code).toEqual(['in', 0, 'const', 0, 'pow', 'neg'])
    expect(ok('IN0 < 1 || IN1 > 2 && IN2 == 3').code).toEqual(['in', 0, 'const', 0, '<', 'in', 1, 'const', 1, '>', 'in', 2, 'const', 2, '==', 'and', 'or'])
    expect(ok('IN0 % 3 + recip(IN1)').code).toEqual(['in', 0, 'const', 0, 'mod', 'in', 1, '1/x', '+'])
    expect(ok('-3 + pi').constants).toEqual([-3, Math.fround(Math.PI)])
    expect(ok('~IN0 & 0xFF ^ 0b101', bits).code).toEqual(['in', 0, '~', 'const', 0, '&', 'const', 1, '^'])
  })
})

describe('formula errors point at their place', () => {
  it.each([
    ['IN0 + ', 'missing', 6, 6],
    ['(IN0 + 1', 'not closed', 0, 1],
    ['IN0 + 1)', "without its '('", 7, 8],
    ['min(IN0)', 'takes 2 arguments', 0, 8],
    ['sqr(IN0)', 'did you mean sq', 0, 3],
    ['IN0 IN1', 'operator is missing', 4, 7],
    ['IN16 + 1', 'at most 16 inputs', 0, 4],
    ['IN0 # 2', 'not part of a formula', 4, 5],
    ['sin IN0', 'in brackets', 0, 3],
  ])('%s', (text, message, start, end) => {
    expect(error(text)).toEqual({ message: expect.stringContaining(message), start, end })
  })

  it('checks number ranges and the stack', () => {
    expect(error('IN0 | 0x100000000', bits).message).toContain('0..4294967295')
    expect(error('IN0 + 1', bits).message).toContain("'+' is not an operator of bit formulas (<< >> >>> & ^ | ~)")
    expect(error('-1', bits).message).toContain('no negative')
    const deep = Array.from({ length: 17 }, (_, index) => `IN${index % 16}`).reduceRight((inner, item) => `(${item} + ${inner})`)
    expect(error(deep).message).toContain('the block holds 16')
  })

  it('tokenizes inputs and names apart', () => {
    expect(tokenize('in2+int', 'float').tokens.map((token) => token.kind)).toEqual(['input', 'op', 'name'])
  })
})

describe('RPN → formula', () => {
  it.each([
    '(IN0 + 2) * IN1',
    'IN0 - (IN1 - IN2)',
    'IN0 - IN1 - IN2',
    '-IN0 ^ 2',
    '(-IN0) ^ 2',
    'IN0 > IN1 ? IN0 : IN1',
    '(IN0 ? IN1 : IN2) + 1',
    'min(max(IN0, 0), 100)',
    '!(IN0 && IN1) || IN2',
    'IN0 * -2.5 + 0.1',
  ])('%s reads back the same', (formula) => {
    const compiled = ok(formula)
    expect(decompileExpression(compiled.code, compiled.constants, float)).toBe(formula)
  })

  it('bit formulas, hex above a byte', () => {
    const compiled = ok('(IN0 << 4) | IN1 & 0xF0F0', bits)
    expect(decompileExpression(compiled.code, compiled.constants, bits)).toBe('IN0 << 4 | IN1 & 0xF0F0')
  })

  it('resolves stack plumbing, refuses a broken program', () => {
    expect(decompileExpression(['in', 0, 'dup', '*'], [], float)).toBe('IN0 * IN0')
    expect(decompileExpression(['in', 0, 'in', 1, 'swap', '-'], [], float)).toBe('IN1 - IN0')
    expect(decompileExpression(['in', 0, 'in', 1], [], float)).toBeUndefined()
    expect(decompileExpression(['+'], [], float)).toBeUndefined()
  })
})
