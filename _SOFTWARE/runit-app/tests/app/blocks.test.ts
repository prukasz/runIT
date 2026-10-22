import { describe, expect, it } from 'vitest'
import { createEngine, newBlock } from '@engine'
import type { BlockInstance, Project, Variable } from '@engine'
import { blockName, chipLabel, connect, connectWithFlow, copyBlocks, disconnect, fits, formulaText, groupCount, headline, headlineText, newBlockId, pinsOf, removeBlocks, sentBy, sourceLabel, wiresOf, withGroupCount } from '../../src/app/code/blocks'
import type { BlockContext } from '../../src/app/code/blocks'
import { scopesOf } from '../../src/app/code/canvas/scopes'
import { blockShape, collides, flowY, pinViews, FORMULA_ROW, freeSpot, nearestFreeSpot, GRID, HEADER, pinAnchor, positionOf, readLayout, screenToCanvas, zoomAt } from '../../src/app/code/canvas/layout'

const engine = createEngine()
const variables: Variable[] = [
  { kind: 'value', id: 'v1', name: 'speed', type: 'f32', count: 1 },
  { kind: 'value', id: 'v2', name: 'gains', type: 'f32', count: 3 },
  { kind: 'value', id: 'v3', name: 'on', type: 'bool', count: 1 },
  { kind: 'folder', id: 'f1', name: 'arm', children: [{ kind: 'value', id: 'v4', name: 'angle', type: 'i32', count: 1 }] },
]
const context = (blocks: BlockInstance[]): BlockContext => ({
  catalog: engine.catalog,
  project: { variables, blocks } satisfies Project,
  deviceName: (id) => (id === 1 ? 'GPIO' : undefined),
  pinName: (device, pin) => `#${pin}${device === 1 && pin === 22 ? ' LED blue' : ''}`,
  eventName: (_device, code) => (code === 256 ? 'Rising edge' : undefined),
  actionName: (id) => (id === 10 ? 'Wave' : undefined),
  language: (type) => engine.expression(type),
})
/** Every pin width measured by its characters: the same numbers in a test as anywhere without a canvas. */
const measure = (text: string, _font: string, perChar: number) => text.length * perChar

describe('blocks: ids and names', () => {
  it('a new id is the type and the lowest free number', () => {
    expect(newBlockId([newBlock('periodic1', 'periodic'), newBlock('periodic3', 'periodic')], 'periodic')).toBe('periodic2')
  })

  it('a block is called by its name, else its type, numbered when two would read alike', () => {
    const blocks = [newBlock('a', 'periodic'), newBlock('b', 'periodic'), newBlock('c', 'periodic', { name: 'Blink' }), newBlock('d', 'if')]
    expect(['a', 'b', 'c', 'd'].map((id) => blockName(engine.catalog, blocks, id))).toEqual(['Every 1', 'Every 2', 'Blink', 'If'])
  })

  it('a source reads as a path or as the block and its pin, never an id', () => {
    const blocks = [newBlock('p1', 'periodic'), newBlock('e1', 'expr', { groups: { inputs: 2 } })]
    const ctx = context(blocks)
    expect(sourceLabel(ctx, { kind: 'variable', path: { root: 'f1', steps: [{ kind: 'name', name: 'angle' }] } })).toBe('arm.angle')
    expect(sourceLabel(ctx, { kind: 'eno', block: 'p1' })).toBe('Every.Tick')
    expect(sourceLabel(ctx, { kind: 'output', block: 'e1', pin: 0 })).toBe(`${engine.catalog.get('expr')!.alias}.${pinsOf(engine.catalog, blocks[1]!)!.outputs[0]!.pin.alias}`)
  })

  it('an output that writes a variable reads as the variable, its pin keeps its own name', () => {
    const writer = newBlock('e1', 'expr', { outputs: [{ path: { root: 'v1', steps: [] } }] })
    expect(sourceLabel(context([writer]), { kind: 'output', block: 'e1', pin: 0 })).toBe('speed')
    expect(pinViews(context([writer]), writer).outputs[0]!.quiet).toBeUndefined()
  })

  it('a long label is cut from its start for a chip', () => {
    expect(chipLabel('short')).toBe('short')
    expect(chipLabel('robot.arm.temperature')).toBe('…temperature')
  })
})

describe('blocks: wiring', () => {
  it('an input takes a source in place of what it had, the enables one more (once), an output only a variable', () => {
    let b = newBlock('e1', 'expr')
    b = connect(b, { block: 'e1', side: 'in', index: 1 }, { kind: 'variable', path: { root: 'v1', steps: [] } })
    expect(b.inputs).toEqual([null, { kind: 'variable', path: { root: 'v1', steps: [] } }])
    b = connect(b, { block: 'e1', side: 'en' }, { kind: 'eno', block: 'p1' })
    b = connect(b, { block: 'e1', side: 'en' }, { kind: 'eno', block: 'p1' })
    expect(b.enables).toHaveLength(1)
    expect(connect(b, { block: 'e1', side: 'out', index: 0 }, { kind: 'eno', block: 'p1' })).toBe(b)
    b = connect(b, { block: 'e1', side: 'out', index: 0 }, { kind: 'variable', path: { root: 'v1', steps: [] } })
    expect(b.outputs).toEqual([{ path: { root: 'v1', steps: [] } }])
    b = disconnect(disconnect(b, { block: 'e1', side: 'en', index: 0 }), { block: 'e1', side: 'in', index: 1 })
    expect(b.enables).toEqual([])
    expect(b.inputs).toEqual([null, null])
  })

  it('the wires are the inputs and enables that read another block', () => {
    const blocks = [newBlock('p1', 'periodic'), newBlock('e1', 'expr', { inputs: [{ kind: 'output', block: 'x', pin: 0 }, { kind: 'variable', path: { root: 'v1', steps: [] } }], enables: [{ kind: 'eno', block: 'p1' }] }), newBlock('e2', 'expr', { inputs: [{ kind: 'output', block: 'e1', pin: 0 }] })]
    expect(wiresOf(blocks)).toEqual([
      { from: { block: 'p1', pin: 'eno' }, to: { block: 'e1', side: 'en', index: 0 } },
      { from: { block: 'e1', pin: 0 }, to: { block: 'e2', side: 'in', index: 0 } },
    ])
  })

  it('numbers and booleans fit numbers; a whole object only an object pin; Run when no object', () => {
    const blocks = [newBlock('s1', 'set'), newBlock('e1', 'expr', { groups: { inputs: 1 } })]
    const ctx = context(blocks)
    const whole = { kind: 'variable' as const, path: { root: 'v2', steps: [] } }
    const element = { kind: 'variable' as const, path: { root: 'v2', steps: [{ kind: 'index' as const, index: 1 }] } }
    expect(sentBy(ctx, whole)).toBe('object')
    expect(sentBy(ctx, element)).toBe('number')
    expect(sentBy(ctx, { kind: 'variable', path: { root: 'v3', steps: [] } })).toBe('bool')
    const setIn = pinsOf(engine.catalog, blocks[0]!)!.inputs[0]
    const exprIn = pinsOf(engine.catalog, blocks[1]!)!.inputs[0]
    expect(fits(setIn, 'in', 'object', whole)).toBe(true)
    expect(fits(exprIn, 'in', 'object', whole)).toBe(false)
    expect(fits(exprIn, 'in', 'bool', element)).toBe(true)
    expect(fits(undefined, 'en', 'object', whole)).toBe(false)
    expect(fits(undefined, 'out', 'number', { kind: 'eno', block: 'x' })).toBe(false)
  })

  it('removing a block takes off what the others read from it', () => {
    const blocks = [newBlock('p1', 'periodic'), newBlock('e1', 'expr', { inputs: [{ kind: 'output', block: 'p1', pin: 0 }], enables: [{ kind: 'eno', block: 'p1' }, { kind: 'variable', path: { root: 'v3', steps: [] } }] })]
    const [left] = removeBlocks(blocks, ['p1'])
    expect(left).toMatchObject({ id: 'e1', inputs: [null], enables: [{ kind: 'variable', path: { root: 'v3', steps: [] } }] })
  })

  it('copies keep their own wiring, read what the originals read, and write no variable', () => {
    const blocks = [newBlock('periodic1', 'periodic'), newBlock('expr1', 'expr', { inputs: [{ kind: 'variable', path: { root: 'v1', steps: [] } }], enables: [{ kind: 'eno', block: 'periodic1' }], outputs: [{ path: { root: 'v1', steps: [] } }] })]
    const { added, ids } = copyBlocks(blocks, blocks)
    expect([...ids.values()]).toEqual(['periodic2', 'expr2'])
    expect(added[1]).toMatchObject({ id: 'expr2', enables: [{ kind: 'eno', block: 'periodic2' }], inputs: [{ kind: 'variable', path: { root: 'v1', steps: [] } }], outputs: [null] })
  })

  it('a group grows and shrinks within its range; what was past the new end goes', () => {
    const type = engine.catalog.get('expr')!
    let b = newBlock('e1', 'expr')
    const start = groupCount(type, b, 'inputs')!
    b = withGroupCount(type, b, 'inputs', start.max + 5)
    expect(groupCount(type, b, 'inputs')!.count).toBe(start.max)
    b = connect(b, { block: 'e1', side: 'in', index: 2 }, { kind: 'variable', path: { root: 'v1', steps: [] } })
    b = withGroupCount(type, b, 'inputs', 2)
    expect(b.inputs).toHaveLength(2)
  })
})

describe('blocks: a wire keeps the flow', () => {
  const out = { kind: 'output', block: 'e1', pin: 0 } as const
  const into = { block: 'h1', side: 'in', index: 1 } as const
  it('a block with no Run when also runs on the When done of the block it reads', () => {
    const blocks = [newBlock('e1', 'expr'), newBlock('h1', 'hbridge')]
    expect(connectWithFlow(blocks, blocks[1]!, into, out).enables).toEqual([{ kind: 'eno', block: 'e1' }])
  })
  it('not over a Run when of its own, a loop back, or a block that sends something when its enable falls', () => {
    const own = newBlock('h1', 'hbridge', { enables: [{ kind: 'variable', path: { root: 'v3', steps: [] } }] })
    expect(connectWithFlow([newBlock('e1', 'expr'), own], own, into, out).enables).toEqual(own.enables)
    const back = [newBlock('e1', 'expr', { enables: [{ kind: 'eno', block: 'h1' }] }), newBlock('h1', 'hbridge')]
    expect(connectWithFlow(back, back[1]!, into, out).enables).toEqual([])
    const braking = newBlock('h1', 'hbridge', { values: { idle: 2305 } })
    expect(connectWithFlow([newBlock('e1', 'expr'), braking], braking, into, out).enables).toEqual([])
  })
})

describe('blocks: the header', () => {
  it('fills the descriptor\'s words with the values, the device and the pin by their names', () => {
    const io = newBlock('io1', 'io', { variant: 'set_level', device: 1, values: { pin: 22 } })
    const periodic = newBlock('p1', 'periodic', { values: { period: 250 } })
    const ctx = context([io, periodic])
    expect(headlineText(headline(ctx, periodic))).toMatch(/^Every 250 /)
    expect(headlineText(headline(ctx, io))).toMatch(/^#22 LED blue = /)
    expect(headline(ctx, periodic).filter((p) => p.value).map((p) => p.text)[0]).toBe('250')
  })

  it('a wired field reads as what it reads', () => {
    const periodic = newBlock('p1', 'periodic', { inputs: [{ kind: 'variable', path: { root: 'v1', steps: [] } }] })
    expect(headlineText(headline(context([periodic]), periodic))).toMatch(/^Every speed /)
  })

  it('a formula writes its wired inputs by name', () => {
    const e = newBlock('e1', 'expr', { groups: { inputs: 2 }, inputs: [{ kind: 'variable', path: { root: 'v1', steps: [] } }], editor: { text: 'in0 * 2 + in1' } })
    expect(formulaText(context([e]), e)).toBe('speed * 2 + in1')
  })
})

describe('canvas layout', () => {
  it('a When done band meets blocks of any height at one height: level when their tops are, centred on a one-row block', () => {
    const ctx = context([])
    const short = blockShape(ctx, newBlock('p1', 'periodic'))
    const tall = blockShape(ctx, newBlock('h1', 'hbridge'))
    expect(short.height).not.toBe(tall.height)
    expect(flowY({ x: 0, y: 100 }, short)).toBe(flowY({ x: 400, y: 100 }, tall))
    expect(flowY({ x: 0, y: 100 }, short)).toBe(100 + short.height / 2)
  })

  it('a block is on the grid, at least six cells wide, with a row for its formula', () => {
    const plain = newBlock('p1', 'periodic')
    const formula = newBlock('e1', 'expr', { editor: { text: 'in0 + 1' } })
    const ctx = context([plain, formula])
    const a = blockShape(ctx, plain, measure)
    const b = blockShape(ctx, formula, measure)
    expect(a.width % GRID).toBe(0)
    expect(a.width).toBeGreaterThanOrEqual(6 * GRID)
    expect(b.formula).toBe(true)
    expect(b.height).toBe(HEADER + Math.max(1, b.inputs.length, b.outputs.length) * b.row + FORMULA_ROW)
  })

  it('a wire meets an input on the left edge, an output on the right, Run when at the header', () => {
    const e = newBlock('e1', 'expr', { groups: { inputs: 2 } })
    const shape = blockShape(context([e]), e, measure)
    const at = { x: 100, y: 40 }
    expect(pinAnchor(at, shape, 'en')).toEqual({ x: 100, y: 40 + HEADER / 2 })
    expect(pinAnchor(at, shape, { side: 'in', index: 1 })).toEqual({ x: 100, y: 40 + HEADER + shape.row * 1.5 })
    expect(pinAnchor(at, shape, { side: 'out', index: 0 }).x).toBe(100 + shape.width)
  })

  it('zooming keeps the point under the pointer', () => {
    const view = { x: 30, y: -20, zoom: 1 }
    const anchor = { x: 200, y: 150 }
    const zoomed = zoomAt(view, 1.5, anchor)
    expect(screenToCanvas(zoomed, anchor)).toEqual(screenToCanvas(view, anchor))
    expect(zoomAt(view, 100, anchor).zoom).toBe(2)
  })

  it('a new block goes down past what it would cover', () => {
    expect(freeSpot({ x: 0, y: 0 }, { width: 120, height: 60 }, [{ x: 0, y: 0, width: 140, height: 80 }])).toEqual({ x: 0, y: 100 })
  })

  it('a block the layout does not place stands in a column, in project order', () => {
    const blocks = [newBlock('a', 'if'), newBlock('b', 'if'), newBlock('c', 'if')]
    const layout = readLayout({ blocks: { b: { x: 300, y: 300 } }, chips: [] })
    expect(blocks.map((b) => positionOf(layout, blocks, b.id))).toEqual([{ x: 0, y: 0 }, { x: 300, y: 300 }, { x: 0, y: 8 * GRID }])
  })
})

describe('canvas scopes and collisions', () => {
  const box = (x: number, y: number) => ({ x, y, width: 100, height: 60 })
  const at: Record<string, { x: number; y: number }> = { if1: { x: 0, y: 0 }, a: { x: 200, y: 0 }, b: { x: 400, y: 0 }, c: { x: 200, y: 200 }, for1: { x: 200, y: 400 }, d: { x: 400, y: 400 } }
  const rectOf = (b: BlockInstance) => box(at[b.id]!.x, at[b.id]!.y)

  it('a branch holds what its pin runs and what those run on and on; a loop body is a scope inside it', () => {
    const blocks = [
      newBlock('if1', 'if'),
      newBlock('a', 'periodic', { enables: [{ kind: 'output', block: 'if1', pin: 0 }] }),
      newBlock('b', 'periodic', { enables: [{ kind: 'eno', block: 'a' }] }),
      newBlock('c', 'periodic', { enables: [{ kind: 'output', block: 'if1', pin: 1 }] }),
      newBlock('for1', 'for', { enables: [{ kind: 'eno', block: 'c' }] }),
      newBlock('d', 'periodic', { enables: [{ kind: 'eno', block: 'for1' }] }),
    ]
    const scopes = scopesOf(engine.catalog, blocks, rectOf)
    const of = (key: string) => scopes.find((s) => s.key === key)!
    expect([...of('if1:0').members].sort()).toEqual(['a', 'b'])
    expect([...of('if1:1').members].sort()).toEqual(['c', 'd', 'for1'])
    expect(of('for1:eno').members).toEqual(['d'])
    expect(of('for1:eno').depth).toBe(1)
    const outer = of('if1:1').rect
    const inner = of('for1:eno').rect
    expect(outer.x < inner.x && outer.y < inner.y && outer.x + outer.width > inner.x + inner.width && outer.y + outer.height > inner.y + inner.height).toBe(true)
    expect(of('if1:0').label).toMatch(/^If · /)
  })

  it('a dropped block settles at the nearest spot that covers no other', () => {
    const taken = [{ x: 0, y: 0, width: 200, height: 100 }]
    const spot = nearestFreeSpot({ x: 20, y: 20 }, { width: 100, height: 60 }, taken)
    expect(collides([{ ...spot, width: 100, height: 60 }], taken)).toBe(false)
    expect(nearestFreeSpot({ x: 400, y: 0 }, { width: 100, height: 60 }, taken)).toEqual({ x: 400, y: 0 })
  })
})
