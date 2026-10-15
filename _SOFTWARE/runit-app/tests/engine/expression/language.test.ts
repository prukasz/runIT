import { describe, expect, it } from 'vitest'
import { blk, engine, num, project, read } from '../helpers'

const lang = (block: string) => engine.expression(block)!

describe('the language of a block', () => {
  it('is read from the opcode enum of that block: float for expr, u32 for expr_bit, none for other blocks', () => {
    const f = lang('expr')
    const b = lang('expr_bit')
    expect([f.flavor, b.flavor]).toEqual(['float', 'bit'])
    expect(f.context.opcodes.get('ADD')).not.toBe(undefined)
    expect(f.functions.map((x) => x.name)).toContain('sqrt')
    expect(f.functions.map((x) => x.name)).not.toContain('rol')
    expect(b.functions.map((x) => x.name)).toContain('rol')
    expect(b.functions.map((x) => x.name)).not.toContain('sqrt')
    expect(engine.expression('if')).toBeUndefined()
    expect(engine.expression('nope')).toBeUndefined()
  })

  it('lists operators with their precedence and functions with the firmware description', () => {
    const f = lang('expr')
    const prec = (symbol: string) => f.operators.find((o) => o.symbol === symbol && o.arity === 2)!.precedence
    expect(prec('*')).toBeGreaterThan(prec('+'))
    expect(prec('^')).toBeGreaterThan(prec('*'))
    expect(f.functions.find((x) => x.name === 'min')).toMatchObject({ op: 'MIN', arity: 2 })
    expect(f.functions.every((x) => x.description !== '')).toBe(true)
  })

  it('compiles through the language, with a wired check', () => {
    expect(lang('expr').compile('in0 + 1', 1).ok).toBe(true)
    expect(lang('expr').compile('in0 + 1', 1, () => false).ok).toBe(false)
  })

  it('knows true, false and the C-like logic operators (the same bytes as the words)', () => {
    const f = lang('expr')
    const bytes = (text: string) => {
      const r = f.compile(text, 2)
      if (!r.ok) throw new Error(`${text}: ${r.error.message}`)
      return f.state(r.formula)
    }
    expect(bytes('in0 && in1')).toEqual(bytes('in0 and in1'))
    expect(bytes('in0 || in1')).toEqual(bytes('in0 or in1'))
    expect(bytes('!in0')).toEqual(bytes('not in0'))
    expect(bytes('true + false')).toEqual(bytes('1 + 0'))
    expect(f.compile('in0 != 2', 1).ok).toBe(true)
  })
})

describe('tokens', () => {
  const kinds = (block: string, text: string) => lang(block).tokenize(text).tokens.map((t) => `${t.kind}:${t.text}`)

  it('split a formula for colouring, with the place of each', () => {
    expect(kinds('expr', 'min(in0, 2.5) > 1 ? 1 : 0')).toEqual(['name:min', 'open:(', 'input:in0', 'comma:,', 'number:2.5', 'close:)', 'op:>', 'number:1', 'question:?', 'number:1', 'colon::', 'number:0'])
    expect(kinds('expr', 'a mod b and c')).toEqual(['name:a', 'op:mod', 'name:b', 'op:and', 'name:c'])
    expect(lang('expr').tokenize('in0 + 12').tokens[2]).toMatchObject({ from: 6, to: 8 })
  })

  it('stop at a character that belongs to nothing, and say where', () => {
    const r = lang('expr').tokenize('in0 + #')
    expect(r.tokens).toHaveLength(2)
    expect(r.error).toMatchObject({ from: 6, to: 7 })
  })

  it('read hex and binary numbers in the bit block only', () => {
    expect(kinds('expr_bit', '0xFF & 0b11')).toEqual(['number:0xFF', 'op:&', 'number:0b11'])
  })
})

describe('decompiling', () => {
  const roundTrip = (block: string, text: string, inputs = 3) => {
    const language = lang(block)
    const first = language.compile(text, inputs)
    if (!first.ok) throw new Error(`${text}: ${first.error.message}`)
    const state = language.state(first.formula)
    const back = language.decompile(state)
    if (!back.ok) throw new Error(`${text}: ${back.reason}`)
    const second = language.compile(back.text, inputs)
    if (!second.ok) throw new Error(`${text} -> ${back.text}: ${second.error.message}`)
    expect(language.state(second.formula), `${text} -> ${back.text}`).toEqual(state) // reads back as the same bytes
    return back.text
  }

  const floats = [
    'in0 + in1 * 2', '(in0 + in1) * 2', 'in0 - (in1 - 1)', 'in0 - in1 - 1', 'in0 / (in1 * 2)', '-in0', '-(in0 + 1)', '-2 ^ 2', '(-2) ^ 2', '2 ^ 3 ^ 2', '(2 ^ 3) ^ 2', '2 ^ -1', '(in0 + 1) ^ 2',
    'in0 < in1 ? in0 : in1', '(in0 ? 1 : 2) + 3', 'in0 ? in1 ? 1 : 2 : 3', 'min(in0, in1) + abs(-1)', 'in0 mod 4', 'in0 and in1 or in2', 'in0 and (in1 or in2)', 'not (in0 > 1)',
    'in0 > 1 and in1 > 1', 'select(in0, 10, 20)', 'sqrt(sq(in0))', 'pi * in0', '0.1 + 0.2', '1e-3 * in0', 'recip(in0)', 'in0 xor in1', 'ln(in0) + log10(in1)', '-(-in0)',
  ]
  for (const text of floats) it(`float: ${text}`, () => void roundTrip('expr', text))

  const bits = ['in0 & 0xFF', 'in0 | in1 << 4', '(in0 | in1) << 4', 'in0 ^ in1 & in2', '~in0', '~(in0 & in1)', 'rol(in0, 8)', 'setbit(in0, 3)', 'in0 >>> 4', 'popcount(in0) | 1', '0xFFFFFFFF & in0']
  for (const text of bits) it(`bit: ${text}`, () => void roundTrip('expr_bit', text))

  it('writes the plain text of a formula', () => {
    expect(roundTrip('expr', '( in0+in1 )*2')).toBe('(in0 + in1) * 2')
    expect(roundTrip('expr', 'in0 and in1')).toBe('in0 && in1')
    expect(roundTrip('expr_bit', 'in0&255')).toBe('in0 & 255')
  })

  /** A block state with these constants (floats) and this code. */
  const handmade = (constants: number[], code: number[]) => {
    const s = new Uint8Array(4 + constants.length * 4 + code.length)
    const v = new DataView(s.buffer)
    v.setUint8(0, constants.length)
    v.setUint16(2, code.length, true)
    constants.forEach((c, i) => v.setFloat32(4 + i * 4, c, true))
    s.set(code, 4 + constants.length * 4)
    return s
  }
  const op = (n: string) => lang('expr').context.opcodes.get(n)!

  it('resolves dup of hand-made code', () => {
    expect(lang('expr').decompile(handmade([2], [op('K'), 0, op('DUP'), op('MUL'), op('END')]))).toEqual({ ok: true, text: '2 * 2' })
  })

  it('refuses code that is not one formula, and says why', () => {
    const f = lang('expr')
    expect(f.decompile(handmade([], [op('ADD')]))).toMatchObject({ ok: false, reason: expect.stringMatching(/stack short/) })
    expect(f.decompile(handmade([], [op('IN'), 0, op('IN'), 1]))).toMatchObject({ ok: false, reason: expect.stringMatching(/leaves 2/) })
    expect(f.decompile(handmade([], [op('K'), 3]))).toMatchObject({ ok: false, reason: expect.stringMatching(/Constant 3/) })
    expect(f.decompile(handmade([], [0xee]))).toMatchObject({ ok: false, reason: expect.stringMatching(/Unknown opcode/) })
    expect(f.decompile(new Uint8Array(2))).toMatchObject({ ok: false })
  })

  it('reads back the state the compiler put in a project', () => {
    const r = engine.compile(project([num('x'), num('y')], [blk('e', 'expr', { groups: { inputs: 2 }, inputs: [read('x'), read('y')], editor: { text: 'in0 * 2 + in1' } })]))
    expect(lang('expr').decompile(r.ir!.blocks[0]!.state)).toEqual({ ok: true, text: 'in0 * 2 + in1' })
  })
})
