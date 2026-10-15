import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import { compileFormula, formulaContext, formulaState, OP_INFO } from '@engine'
import type { Flavor, FormulaContext } from '@engine'
import { generatedEnums } from '@engine/generated'
import { blk, engine, num, project, read } from '../helpers'

const context = (flavor: Flavor): FormulaContext => formulaContext(flavor, generatedEnums, flavor === 'float' ? 'vm_expr_op_e' : 'vm_bit_op_e')

/* A stack machine written here, from the descriptions in vm_expr_core.h, to run what the compiler produced. */
const run = (flavor: Flavor, text: string, inputs: number[]): number => {
  const ctx = context(flavor)
  const result = compileFormula(ctx, text, inputs.length, () => true)
  if (!result.ok) throw new Error(`${text}: ${result.error.message}`)
  const byValue = new Map([...ctx.opcodes].map(([name, value]) => [value, name]))
  const { code, constants } = result.formula
  const stack: number[] = []
  const bit = flavor === 'bit'
  const u = (x: number) => x >>> 0
  const binary: Record<string, (a: number, b: number) => number> = bit
    ? {
        AND: (a, b) => u(a & b), OR: (a, b) => u(a | b), XOR: (a, b) => u(a ^ b),
        SHL: (a, b) => (b >= 32 ? 0 : u(a << b)), SHR: (a, b) => (b >= 32 ? 0 : u(a >>> b)), SAR: (a, b) => u(b >= 32 ? a >> 31 : a >> b),
        ROL: (a, b) => u((a << (b & 31)) | (a >>> ((32 - (b & 31)) & 31))), ROR: (a, b) => u((a >>> (b & 31)) | (a << ((32 - (b & 31)) & 31))),
        GET: (a, b) => (a >>> (b & 31)) & 1, SET: (a, b) => u(a | (1 << (b & 31))), CLR: (a, b) => u(a & ~(1 << (b & 31))), TGL: (a, b) => u(a ^ (1 << (b & 31))),
      }
    : {
        ADD: (a, b) => a + b, SUB: (a, b) => a - b, MUL: (a, b) => a * b, DIV: (a, b) => a / b, MOD: (a, b) => a % b, POW: (a, b) => a ** b,
        ROOT: (a, b) => a ** (1 / b), MIN: Math.min, MAX: Math.max, ATAN2: Math.atan2, HYPOT: Math.hypot,
        LT: (a, b) => +(a < b), LE: (a, b) => +(a <= b), GT: (a, b) => +(a > b), GE: (a, b) => +(a >= b), EQ: (a, b) => +(a === b), NE: (a, b) => +(a !== b),
        AND: (a, b) => +(a !== 0 && b !== 0), OR: (a, b) => +(a !== 0 || b !== 0), XOR: (a, b) => +((a !== 0) !== (b !== 0)),
      }
  const unary: Record<string, (a: number) => number> = bit
    ? { NOT: (a) => u(~a), POPCNT: (a) => a.toString(2).split('1').length - 1, CLZ: (a) => Math.clz32(a), CTZ: (a) => (a === 0 ? 32 : 31 - Math.clz32(a & -a)), BSWAP: (a) => u(((a & 0xff) << 24) | ((a & 0xff00) << 8) | ((a >>> 8) & 0xff00) | (a >>> 24)) }
    : {
        NEG: (a) => -a, ABS: Math.abs, SQRT: Math.sqrt, SQUARE: (a) => a * a, RECIP: (a) => 1 / a, FLOOR: Math.floor, CEIL: Math.ceil,
        ROUND: (a) => Math.sign(a) * Math.round(Math.abs(a)), TRUNC: Math.trunc, SIGN: Math.sign, SIN: Math.sin, COS: Math.cos, NOT: (a) => +(a === 0),
      }
  for (let pc = 0; pc < code.length; ) {
    const name = byValue.get(code[pc++]!)!
    if (name === 'IN') stack.push(inputs[code[pc++]!]!)
    else if (name === 'K') stack.push(constants[code[pc++]!]!)
    else if (name === 'SEL') {
      const f = stack.pop()!, t = stack.pop()!, c = stack.pop()!
      stack.push(c !== 0 ? t : f)
    } else if (binary[name]) {
      const b = stack.pop()!, a = stack.pop()!
      stack.push(binary[name](a, b))
    } else if (unary[name]) stack.push(unary[name](stack.pop()!))
    else throw new Error(`the reference machine does not know ${name}`)
  }
  expect(stack).toHaveLength(1) // what the board demands: exactly one value left
  return stack[0]!
}

describe('formulas (float)', () => {
  const cases: [string, number[], number][] = [
    ['in0 + in1 * 2', [3, 4], 11],
    ['(in0 + in1) * 2', [3, 4], 14],
    ['in0 - in1 - 1', [10, 3], 6], // left to right
    ['in0 / in1 / 2', [12, 3], 2],
    ['-in0', [3], -3],
    ['2 ^ 3 ^ 2', [], 512], // right to left
    ['-2 ^ 2', [], -4], // a power binds tighter than a minus in front
    ['2 ^ -1', [], 0.5],
    ['in0 < in1 ? in0 : in1', [5, 2], 2],
    ['in0 < in1 ? in0 : in1', [1, 2], 1],
    ['min(in0, in1) + abs(-1)', [5, 2], 3],
    ['in0 mod 4', [10], 2],
    ['in0 % 4', [10], 2],
    ['not in0', [0], 1],
    ['in0 and in1', [1, 0], 0],
    ['in0 or in1', [1, 0], 1],
    ['in0 xor in1', [1, 1], 0],
    ['in0 > 1 and in1 > 1', [2, 3], 1],
    ['select(in0, 10, 20)', [0], 20],
    ['sqrt(sq(in0))', [-7], 7],
    ['round(2.5) + round(-2.5)', [], 0],
    ['hypot(in0, in1)', [3, 4], 5],
    ['pi * 2', [], Math.fround(Math.PI) * 2],
    ['recip(in0)', [4], 0.25],
    ['in0 == 3 ? 1 : in0 == 4 ? 2 : 3', [4], 2],
  ]
  for (const [text, inputs, want] of cases) it(`${text}  with ${JSON.stringify(inputs)}  =  ${want}`, () => expect(run('float', text, inputs)).toBeCloseTo(want, 5))

  it('emits RPN: operands first, an input and a constant with one operand byte each', () => {
    const ctx = context('float')
    const r = compileFormula(ctx, 'in0 + in1 * 2', 2, () => true)
    if (!r.ok) throw new Error(r.error.message)
    const op = (name: string) => ctx.opcodes.get(name)!
    expect([...r.formula.code]).toEqual([op('IN'), 0, op('IN'), 1, op('K'), 0, op('MUL'), op('ADD')])
    expect(r.formula.constants).toEqual([2])
    expect(r.formula.inputs).toEqual([0, 1])
    expect(r.formula.depth).toBe(3)
  })

  it('stores one constant for the same number, and a minus in front of a number is the number', () => {
    const r = compileFormula(context('float'), '2 + 2 + 2 + -2', 0, () => true)
    if (!r.ok) throw new Error(r.error.message)
    expect(r.formula.constants).toEqual([2, -2])
  })

  it('writes the state the board reads: count, rt, code length, constants, code', () => {
    const ctx = context('float')
    const r = compileFormula(ctx, 'in0 * 2.5', 1, () => true)
    if (!r.ok) throw new Error(r.error.message)
    const state = formulaState('float', r.formula)
    const v = new DataView(state.buffer)
    expect([v.getUint8(0), v.getUint8(1), v.getUint16(2, true)]).toEqual([1, 0, r.formula.code.length])
    expect(v.getFloat32(4, true)).toBe(2.5)
    expect(state).toHaveLength(4 + 4 + r.formula.code.length) // vm_expr_size
  })
})

describe('formulas (bit)', () => {
  const cases: [string, number[], number][] = [
    ['in0 & 0xFF', [0x1234], 0x34],
    ['in0 | in1 << 4', [1, 3], 0x31], // a shift binds tighter than an or
    ['1 | 2 & 3', [], 3],
    ['in0 ^ in1', [6, 3], 5],
    ['~in0', [0], 0xffffffff],
    ['in0 >> 4', [0xf0], 0xf],
    ['in0 >>> 4', [0x80000000], 0xf8000000],
    ['rol(in0, 8)', [0x12345678], 0x34567812],
    ['ror(in0, 8)', [0x12345678], 0x78123456],
    ['bit(in0, 3)', [8], 1],
    ['setbit(in0, 0)', [0], 1],
    ['clearbit(in0, 0)', [3], 2],
    ['togglebit(in0, 1)', [3], 1],
    ['popcount(in0)', [0xff], 8],
    ['clz(in0)', [1], 31],
    ['0b1010 | 0x5', [], 0xf],
  ]
  for (const [text, inputs, want] of cases) it(`${text}  with ${JSON.stringify(inputs)}  =  ${want}`, () => expect(run('bit', text, inputs)).toBe(want))

  it('keeps constants as u32', () => {
    const r = compileFormula(context('bit'), 'in0 & 0xFFFFFFFF', 1, () => true)
    if (!r.ok) throw new Error(r.error.message)
    const v = new DataView(formulaState('bit', r.formula).buffer)
    expect(v.getUint32(4, true)).toBe(0xffffffff)
  })

  it('refuses a number that is not 32 bits unsigned', () => {
    const r = compileFormula(context('bit'), 'in0 & 4294967296', 1, () => true)
    expect(r.ok).toBe(false)
  })
})

describe('formula errors say where and what', () => {
  const error = (text: string, inputCount = 2, wired: (pin: number) => boolean = () => true) => {
    const r = compileFormula(context('float'), text, inputCount, wired)
    if (r.ok) throw new Error(`'${text}' compiled`)
    return r.error
  }

  it('an empty formula, an unbalanced bracket, a missing operand', () => {
    expect(error('').message).toMatch(/empty/)
    expect(error('(1 + 2').message).toMatch(/'\)'/)
    expect(error('1 +').message).toMatch(/ends too soon/)
  })

  it('an unknown name, with a suggestion when one is close', () => {
    const e = error('sqr(in0)')
    expect(e.message).toMatch(/Unknown name 'sqr'.*Did you mean/)
    expect([e.from, e.to]).toEqual([0, 3])
  })

  it('a function without its brackets, and with the wrong number of values', () => {
    expect(error('sqrt').message).toMatch(/is a function/)
    expect(error('min(1)').message).toMatch(/takes 2 values, not 1/)
  })

  it('an input the block does not have, or that nothing is wired to', () => {
    expect(error('in3').message).toMatch(/in3 does not exist.*2 inputs/)
    expect(error('in1', 2, (pin) => pin === 0).message).toMatch(/in1 is not wired/)
  })

  it('a formula whose stack the board cannot hold', () => {
    const deep = `${'in0 + ('.repeat(17)}in0${')'.repeat(17)}`
    expect(error(deep).message).toMatch(/stack of 17.*16/) // reported at the first value that does not fit
  })
})

describe('held against the firmware', () => {
  const header = readFileSync(fileURLToPath(new URL('../../../../components/vm/vm_blocks/core/expr_core/vm_expr_core.h', import.meta.url)), 'utf8')
  const cEntries = (flavor: Flavor) => {
    const prefix = flavor === 'float' ? 'EXPR' : 'BIT'
    const entries = new Map<string, string>()
    for (const m of header.matchAll(new RegExp(`\\[VM_${prefix}_([A-Z0-9_]+)\\]\\s*=\\s*\\{(\\d),\\s*(\\d),\\s*VM_EXPR_ARG_([A-Z]+)\\}`, 'g'))) entries.set(m[1]!, `${m[2]}/${m[3]}/${m[4]!.toLowerCase()}`)
    return entries
  }

  for (const flavor of ['float', 'bit'] as const) {
    it(`the ${flavor} opcode table equals vm_expr_core.h`, () => {
      const fromC = cEntries(flavor)
      const mine = new Map(Object.entries(OP_INFO[flavor]).map(([name, i]) => [name, `${i.pops}/${i.pushes}/${i.arg}`]))
      expect([...mine].sort()).toEqual([...fromC].sort())
    })

    it(`every ${flavor} opcode of the generated enum has a stack effect`, () => {
      for (const name of context(flavor).opcodes.keys()) expect(OP_INFO[flavor][name], name).toBeDefined()
    })
  }
})

describe('an expression block', () => {
  const sample = (text: string, wire = true) =>
    project([num('x'), num('y')], [blk('e', 'expr', { groups: { inputs: 2 }, inputs: [read('x'), wire ? read('y') : null], editor: { text } })])

  it('compiles into the block state of the formula', () => {
    const r = engine.compile(sample('in0 + in1'))
    expect(r.diagnostics).toEqual([])
    const block = r.ir!.blocks[0]!
    expect(block.type).toBe(1)
    expect(block.inputs).toHaveLength(2)
    expect(block.outputs).toHaveLength(1) // a result is required: an object of its own
    const v = new DataView(block.state.buffer)
    expect([v.getUint8(0), v.getUint16(2, true)]).toEqual([0, 5]) // no constants; IN 0, IN 1, ADD = 5 bytes
  })

  it('reports a formula error on the block', () => {
    const r = engine.compile(sample('in0 + in1', false))
    expect(r.ok).toBe(false)
    expect(r.diagnostics).toContainEqual(expect.objectContaining({ code: 'expression-error', where: { kind: 'block', id: 'e' } }))
  })

  it('reports a block with no formula', () => {
    const r = engine.compile(project([num('x')], [blk('e', 'expr', { groups: { inputs: 1 }, inputs: [read('x')] })]))
    expect(r.diagnostics.map((d) => d.code)).toContain('expression-empty')
  })

  it('the bit block uses the bit codec', () => {
    const r = engine.compile(project([num('x')], [blk('b', 'expr_bit', { groups: { inputs: 1 }, inputs: [read('x')], editor: { text: 'in0 & 0xF' } })]))
    expect(r.diagnostics).toEqual([])
    expect(r.ir!.blocks[0]!.type).toBe(2)
  })
})
