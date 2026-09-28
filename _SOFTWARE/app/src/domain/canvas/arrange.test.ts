import { describe, expect, it } from 'vitest'
import { runitVmCatalog } from '../descriptors'
import { createProject } from '../project'
import type { CanvasBlock, ObjectPath, ProjectCanvas, ValueNode } from '../project'
import { planVmUpload } from '../upload'
import { arrangeProgram, loopBodyGate } from '.'

const at = (root: string): ObjectPath => ({ root })
const block = (id: string, type: string, x: number, y: number, extra: Partial<CanvasBlock> = {}): CanvasBlock => ({ id, type, x, y, ...extra })
const expr = (id: string, x: number, y: number, inputs: readonly (ObjectPath | null)[], extra: Partial<CanvasBlock> = {}): CanvasBlock =>
  block(id, 'EXPR', x, y, { inputs, expression: { code: ['in', 0] }, ...extra })
const canvas = (id: string, blocks: readonly CanvasBlock[], extra: Partial<ProjectCanvas> = {}): ProjectCanvas => ({ id, name: id, blocks, ...extra })
const ids = (canvases: readonly ProjectCanvas[]) => arrangeProgram(canvases).blocks.map((entry) => entry.id)

describe('execution order', () => {
  it('runs the trees top to bottom, each after what feeds it, then the next canvas', () => {
    const main = canvas('main', [
      expr('late', 0, 400, [at('x')]),
      expr('sink', 0, 0, [at('source:q0')]), // drawn above its source: still runs after it
      expr('source', 200, 100, [at('x')]),
      expr('side', 400, 100, [at('source:q0')]),
    ])
    const second = canvas('second', [expr('next', 0, 0, [at('late:q0')])]) // another canvas: a plain read
    expect(ids([main, second])).toEqual(['source', 'sink', 'side', 'late', 'next'])
    expect(ids([main, { ...second, disabled: true }])).toEqual(['source', 'sink', 'side', 'late'])
  })

  it('cuts a loop in the wires at the topmost block and says so', () => {
    const arranged = arrangeProgram([canvas('c', [expr('a', 0, 0, [at('b:q0')]), expr('b', 0, 100, [at('a:q0')])])])
    expect(arranged.blocks.map((entry) => entry.id)).toEqual(['a', 'b'])
    expect(arranged.diagnostics).toEqual([expect.objectContaining({ severity: 'warning', blockId: 'a', message: expect.stringMatching(/reads 'b' from the previous pass/) })])
  })
})

describe('gates flow down data wires', () => {
  const branch = [
    block('check', 'IF', 0, 0, { inputs: [at('go')] }),
    expr('scale', 0, 100, [at('x')], { enables: [at('check:q0')] }),
    expr('use', 0, 200, [at('scale:q0')]),
    expr('free', 0, 300, [at('scale:q0')], { inheritGates: false }),
  ]

  it('puts the blocks fed from a gated block in its branch, copying the gate and not the ENO', () => {
    const { blocks, gates } = arrangeProgram([canvas('c', branch)])
    expect(blocks.find((entry) => entry.id === 'use')).toMatchObject({ enables: [at('check:q0')] })
    expect(blocks.find((entry) => entry.id === 'use')).not.toHaveProperty('enableMode')
    expect(gates.get('use')?.enables).toEqual([at('check:q0')])
    // Opted out: runs every pass, whatever branch its source is in.
    expect(blocks.find((entry) => entry.id === 'free')).not.toHaveProperty('enables')
    expect(blocks.find((entry) => entry.id === 'free')).not.toHaveProperty('inheritGates')
  })

  it('merges branches with any and narrows a branch with the block\'s own gate', () => {
    const { blocks } = arrangeProgram([canvas('c', [
      ...branch,
      block('tick', 'PERIODIC', 400, 0),
      expr('other', 400, 100, [at('x')], { enables: [at('check:q1')] }),
      expr('merge', 200, 400, [at('use:q0'), at('other:q0')]),
      expr('narrow', 0, 500, [at('use:q0')], { enables: [at('tick:q0')] }),
      expr('mixed', 200, 600, [at('use:q0'), at('other:q0')], { enables: [at('tick:q0')] }),
    ])])
    expect(blocks.find((entry) => entry.id === 'merge')).toMatchObject({ enables: [at('check:q0'), at('check:q1')], enableMode: 'any' })
    expect(blocks.find((entry) => entry.id === 'narrow')).toMatchObject({ enables: [at('tick:q0'), at('check:q0')], enableMode: 'all' })
    const { diagnostics } = arrangeProgram([canvas('c', [...branch, block('tick', 'PERIODIC', 400, 0), expr('other', 400, 100, [at('x')], { enables: [at('check:q1')] }), expr('mixed', 200, 600, [at('use:q0'), at('other:q0')], { enables: [at('tick:q0')] })])])
    expect(diagnostics).toEqual([expect.objectContaining({ severity: 'error', blockId: 'mixed', message: expect.stringMatching(/both "any" and "all"/) })])
  })

  it('keeps a variable on EN to its own block: only block gates flow down the wires', () => {
    const { blocks } = arrangeProgram([canvas('c', [
      block('check', 'IF', 0, 0, { inputs: [at('go')] }),
      expr('armed', 0, 100, [at('x')], { enables: [at('check:q0'), at('armedFlag')], enableMode: 'all' }),
      expr('next', 0, 200, [at('armed:q0')]),
    ])])
    expect(blocks.find((entry) => entry.id === 'next')).toMatchObject({ enables: [at('check:q0')] })
  })

  it('leaves a block fed by variables alone (a variable is a plain read)', () => {
    expect(arrangeProgram([canvas('c', branch)]).blocks.find((entry) => entry.id === 'check')).not.toHaveProperty('enables')
  })
})

describe('loop bodies', () => {
  const loop = (extra: readonly CanvasBlock[] = []) => canvas('c', [
    block('loop', 'FOR', 0, 0, { settings: { k_start: 0, k_end: 3, k_step: 1, max_turns: 10, op: 'ADD', cmp: 'LT' } }),
    expr('after', 0, 50, [at('x')]), // not in the loop, placed between by position
    expr('step', 200, 100, [at('loop:q0')], { enables: [loopBodyGate('loop')] }),
    expr('uses', 200, 200, [at('step:q0')]),
    ...extra,
  ])

  it('reads the FOR\'s ENO on EN as the loop link (older files: <for>:body)', () => {
    expect(loopBodyGate('loop')).toEqual(at('loop:eno'))
    const legacy = arrangeProgram([canvas('c', [block('loop', 'FOR', 0, 0), expr('step', 0, 100, [at('x')], { enables: [at('loop:body')] })])])
    expect(legacy.blocks.map((entry) => entry.id)).toEqual(['loop', 'step'])
    expect(legacy.blocks[0]).toMatchObject({ body: 1 })
  })

  it('places what the FOR gates, and what those feed, right after it', () => {
    const { blocks, gates } = arrangeProgram([loop()])
    expect(blocks.map((entry) => entry.id)).toEqual(['loop', 'step', 'uses', 'after'])
    expect(blocks[0]).toMatchObject({ body: 2 })
    expect(blocks.find((entry) => entry.id === 'step')).not.toHaveProperty('enables') // the body gate never reaches the VM
    expect(gates.get('uses')?.loops).toEqual(['loop'])
  })

  it('nests a loop inside a loop', () => {
    const { blocks, gates, diagnostics } = arrangeProgram([loop([
      block('inner', 'FOR', 400, 100, { enables: [loopBodyGate('loop')] }),
      expr('deep', 400, 200, [at('inner:q0')], { enables: [loopBodyGate('inner')] }),
    ])])
    expect(diagnostics).toEqual([])
    expect(blocks.map((entry) => entry.id)).toEqual(['loop', 'step', 'inner', 'deep', 'uses', 'after'])
    expect(blocks.find((entry) => entry.id === 'loop')).toMatchObject({ body: 4 })
    expect(blocks.find((entry) => entry.id === 'inner')).toMatchObject({ body: 1 })
    expect(gates.get('deep')?.loops).toEqual(['loop', 'inner'])
  })

  it('refuses a body gate of something that is not a loop, and a block in two separate loops', () => {
    const notLoop = arrangeProgram([canvas('c', [expr('a', 0, 0, [at('x')]), expr('b', 0, 100, [at('x')], { enables: [at('a:body')] })])])
    expect(notLoop.diagnostics).toEqual([expect.objectContaining({ blockId: 'b', message: expect.stringMatching(/only a FOR has a body/) })])
    // Another block's ENO on EN is an ordinary enable ("only if it acted").
    const eno = arrangeProgram([canvas('c', [expr('a', 0, 0, [at('x')]), expr('b', 0, 100, [at('x')], { enables: [at('a:eno')] })])])
    expect(eno.diagnostics).toEqual([])
    expect(eno.blocks.find((entry) => entry.id === 'b')).toMatchObject({ enables: [at('a:eno')] })
    const two = arrangeProgram([loop([
      block('other', 'FOR', 600, 0),
      expr('both', 600, 300, [at('step:q0')], { enables: [loopBodyGate('other')] }),
    ])])
    expect(two.diagnostics).toEqual([expect.objectContaining({ blockId: 'both', message: expect.stringMatching(/neither runs inside the other/) })])
  })
})

describe('an arranged canvas compiles', () => {
  it('uploads a branch and a loop the way the canvas draws them', () => {
    const vm = runitVmCatalog()
    const value = (id: string, type: ValueNode['type']): ValueNode => ({ kind: 'value', id, name: id, type, length: 1, mutable: true, retentive: false })
    const project = { ...createProject('arranged'), objects: [value('go', 'B'), value('x', 'F')], canvases: [
      canvas('main', [
        block('check', 'IF', 0, 0, { inputs: [at('go')] }),
        expr('scale', 0, 100, [at('x')], { enables: [at('check:q0')] }),
        expr('use', 0, 200, [at('scale:q0')]),
        block('loop', 'FOR', 300, 0, { settings: { k_start: 0, k_end: 3, k_step: 1, max_turns: 10, op: 'ADD', cmp: 'LT' } }),
        expr('step', 300, 100, [at('loop:q0')], { enables: [loopBodyGate('loop')] }),
      ]),
    ] }
    const plan = planVmUpload(project, vm, { maxFrameBytes: 240 })
    expect(plan.diagnostics.filter((entry) => entry.severity === 'error')).toEqual([])
    expect(plan.ok).toBe(true)
    const placed = plan.program.blocks.blocks
    expect(placed.map((entry) => entry.id)).toEqual(['check', 'scale', 'use', 'loop', 'step'])
    expect(placed.find((entry) => entry.id === 'use')?.enables).toEqual(placed.find((entry) => entry.id === 'scale')?.enables)
    expect(placed.find((entry) => entry.id === 'loop')?.span).toEqual({ start: 4, end: 5 })
  })
})
