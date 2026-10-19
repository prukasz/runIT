import { describe, expect, it } from 'vitest'
import { blk, build, eno, engine, flag, num, out, project, read } from '../helpers'

/** A set block copying `from` to `to`, optionally gated. */
const copy = (id: string, from: string, to: string, enables: ReturnType<typeof eno>[] = []) => blk(id, 'set', { inputs: [read(from), read(to)], enables })

describe('execution order', () => {
  const vars = () => [num('a'), num('b'), num('c'), num('d')]

  it('a block reading another block output runs after it, whatever order the user placed them in', () => {
    const p = project([flag('cond'), ...vars()], [copy('s', 'a', 'b', [out('if1', 0)]), blk('if1', 'if', { inputs: [read('cond')] })])
    expect(engine.arrange(p).order).toEqual(['if1', 's'])
  })

  it('a block reading a variable runs after the block that writes it', () => {
    const p = project(vars(), [copy('reader', 'b', 'c'), copy('writer', 'a', 'b')])
    expect(engine.arrange(p).order).toEqual(['writer', 'reader'])
  })

  it('blocks the wiring does not order keep the order the user gave', () => {
    const p = project(vars(), [copy('one', 'a', 'b'), copy('two', 'c', 'd')])
    expect(engine.arrange(p).order).toEqual(['one', 'two'])
  })

  it('a feedback loop is cut where the user placed it first, with a warning', () => {
    const p = project(vars(), [copy('x', 'a', 'b'), copy('y', 'b', 'a')])
    const arrangement = engine.arrange(p)
    expect(arrangement.order).toEqual(['x', 'y'])
    expect(arrangement.diagnostics.map((d) => d.code)).toEqual(['feedback'])
  })
})

describe('loops', () => {
  const vars = () => [num('a'), num('b'), num('c'), num('d'), num('e')]

  it('the body follows the loop block directly, even when something unrelated was placed between', () => {
    const p = project(vars(), [blk('for1', 'for'), copy('free', 'd', 'e'), copy('s1', 'a', 'b', [eno('for1')]), copy('s2', 'b', 'c')])
    const arrangement = engine.arrange(p)
    // s2 reads what s1 writes, so it is in the body too
    expect(arrangement.order).toEqual(['for1', 's1', 's2', 'free'])
    expect(arrangement.spans.get('for1')).toEqual({ start: 1, end: 3 })
    expect(arrangement.diagnostics).toEqual([])
  })

  it('a block with a Run when from outside the loop runs after the loop, though it reads what the body writes', () => {
    const p = project([flag('cond'), ...vars()], [
      blk('for1', 'for'),
      copy('s1', 'a', 'b', [eno('for1')]),
      blk('if1', 'if', { inputs: [read('cond')] }),
      copy('gated', 'b', 'c', [out('if1', 0)]),
      copy('after', 'c', 'd'),
    ])
    const arrangement = engine.arrange(p)
    expect(arrangement.spans.get('for1')).toEqual({ start: 1, end: 2 })
    expect(arrangement.order.indexOf('gated')).toBeGreaterThan(arrangement.order.indexOf('s1'))
    expect(arrangement.order.indexOf('after')).toBeGreaterThan(arrangement.order.indexOf('gated'))
    expect(arrangement.diagnostics).toEqual([])
  })

  it('what the body reads from outside is placed before the loop block', () => {
    const p = project(vars(), [blk('for1', 'for'), copy('late', 'd', 'a'), copy('s1', 'a', 'b', [eno('for1')])])
    const arrangement = engine.arrange(p)
    expect(arrangement.order).toEqual(['late', 'for1', 's1'])
    expect(arrangement.spans.get('for1')).toEqual({ start: 2, end: 3 })
  })

  it('loops nest: the inner body is inside the outer', () => {
    const p = project(vars(), [blk('outer', 'for'), blk('inner', 'for', { enables: [eno('outer')] }), copy('s', 'a', 'b', [eno('inner')])])
    const arrangement = engine.arrange(p)
    expect(arrangement.order).toEqual(['outer', 'inner', 's'])
    expect(arrangement.spans.get('outer')).toEqual({ start: 1, end: 3 })
    expect(arrangement.spans.get('inner')).toEqual({ start: 2, end: 3 })
  })

  it('compiles the span of each loop into its state, by position', () => {
    const r = build(project(vars(), [blk('outer', 'for'), blk('inner', 'for', { enables: [eno('outer')] }), copy('s', 'a', 'b', [eno('inner')])]))
    const spanOf = (i: number) => {
      const v = new DataView(r.ir!.blocks[i]!.state.buffer)
      return [v.getUint16(0, true), v.getUint16(2, true)]
    }
    expect(spanOf(0)).toEqual([1, 3])
    expect(spanOf(1)).toEqual([2, 3])
  })

  it('a loop with nothing in it is an error', () => {
    expect(engine.arrange(project(vars(), [blk('for1', 'for')])).diagnostics.map((d) => d.code)).toContain('loop-empty')
  })

  it('loops nested deeper than the board runs are an error (the top-level run counts as one level)', () => {
    const chain = (n: number) =>
      Array.from({ length: n }, (_, i) => blk(`f${i}`, 'for', { enables: i === 0 ? [] : [eno(`f${i - 1}`)] })).concat([copy('s', 'a', 'b', [eno(`f${n - 1}`)])])
    const codes = (n: number) => engine.arrange(project(vars(), chain(n))).diagnostics.map((d) => d.code)
    expect(codes(3)).toEqual([])
    expect(codes(4)).toContain('ERR_VM_EXEC_SPAN_DEPTH')
  })

  it('a loop fed by its own body is an error', () => {
    const p = project(vars(), [blk('for1', 'for', { inputs: [read('b')] }), copy('s1', 'a', 'b', [eno('for1')])])
    expect(engine.arrange(p).diagnostics.map((d) => d.code)).toContain('loop-feeds-itself')
  })
})
