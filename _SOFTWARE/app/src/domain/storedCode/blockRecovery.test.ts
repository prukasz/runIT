import { describe, expect, it } from 'vitest'
import { placeBlocks } from '../canvas'
import { runitCommandCatalog, runitDeviceCatalog, runitStreamCatalog, runitVmCatalog } from '../descriptors'
import { createProject } from '../project'
import type { ObjectNode, ObjectPath, ProgramBlock, ProjectDocument, ValueNode } from '../project'
import { boardDefaultSettings, runitSettingsIds } from '../upload'
import { buildStoredCode, decodeStoredCode } from '.'
import type { StoredCodeContext } from '.'

/*
 * Blocks and accessors survive the stored code: build the code of a program,
 * decode it, build the recovered project again and get the same frames.
 */

const ctx: StoredCodeContext = { vm: runitVmCatalog(), commands: runitCommandCatalog(), layout: runitStreamCatalog().ble, ids: runitSettingsIds(), devices: runitDeviceCatalog() }
const defaults = boardDefaultSettings()
const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join('')
const value = (id: string, type: string, initial?: ValueNode['value'], length = 1): ValueNode => ({ kind: 'value', id, name: id, type, length, ...(initial ? { value: initial } : {}), mutable: true, retentive: false })
const at = (root: string): ObjectPath => ({ root })
const tick = at('every:eno')

// The block samples that run on the board (compiler/programSamples.test.ts): twelve types, dynamic accessors, a loop, expressions.
const objects: readonly ObjectNode[] = [
  value('count', 'F'),
  value('sel', 'U8', [2]),
  value('table', 'U32', [100, 200, 300, 400], 4),
  value('copy', 'F'),
  value('acc', 'F'),
]
const blocks: readonly ProgramBlock[] = [
  { id: 'every', type: 'PERIODIC', settings: { period: 100, time_base: 'MS' } },
  { id: 'counter', type: 'EXPR', inputs: [at('count')], outputs: ['count'], enables: [tick], expression: { constants: [1], code: ['in', 0, 'const', 0, '+'] } },
  { id: 'pick', type: 'EXPR', inputs: [{ root: 'table', steps: [{ kind: 'dynamic', index: at('sel') }] }], enables: [tick], expression: { code: ['in', 0] } },
  { id: 'copier', type: 'SET', inputs: [at('pick:q0'), at('copy')] },
  { id: 'above', type: 'EXPR', inputs: [at('count')], enables: [tick], expression: { constants: [3], code: ['in', 0, 'const', 0, '>'] } },
  { id: 'branch', type: 'IF', inputs: [at('above:q0')], eno: true },
  { id: 'hold', type: 'LATCH', inputs: [at('branch:q0'), null], settings: { mode: 'SET_DOMINANT' } },
  { id: 'delay', type: 'TIMER', inputs: [at('hold:q0')], settings: { mode: 'TON', time_base: 'MS', pt: 300 } },
  { id: 'rise', type: 'EDGE', inputs: [at('count')], settings: { edge_type: 'RISING', change_by: 1 } },
  { id: 'seen', type: 'LATCH', inputs: [at('rise:q0'), null] },
  { id: 'route', type: 'SWITCH', inputs: [at('sel')], outputs: [null, null, null] },
  { id: 'loop', type: 'FOR', body: 1, settings: { k_start: 0, k_end: 3, k_step: 1, max_turns: 10, op: 'ADD', cmp: 'LT' } },
  { id: 'sum', type: 'EXPR', inputs: [at('acc'), at('loop:q0')], outputs: ['acc'], expression: { constants: [1], code: ['in', 0, 'in', 1, '+', 'const', 0, '+'] } },
]

const size = () => ({ width: 200, height: 100 })
const projectOf = (list: readonly ProgramBlock[], nodes: readonly ObjectNode[]): ProjectDocument => ({ ...createProject('t'), objects: nodes, canvases: [{ id: 'c', name: 'Main', blocks: placeBlocks(list, size) }] })
const build = (project: ProjectDocument) => buildStoredCode({ project, settings: defaults, boardDefaults: defaults }, ctx)
const recover = (frames: readonly Uint8Array[]) => decodeStoredCode(frames, defaults, ctx, 't')
const errorsOf = (diagnostics: readonly { severity: string }[]) => diagnostics.filter((entry) => entry.severity === 'error')

describe('stored code: blocks and accessors', () => {
  const original = build(projectOf(blocks, objects))
  const frames = original.steps.map((step) => step.frame)
  const recovered = recover(frames)

  it('builds the program', () => {
    expect(errorsOf(original.diagnostics)).toEqual([])
    expect(original.ok).toBe(true)
  })

  it('reads every block back, in order, with generated IDs', () => {
    expect(errorsOf(recovered.diagnostics)).toEqual([])
    expect(recovered.blocks.map((block) => block.type)).toEqual(blocks.map((block) => block.type))
    expect(recovered.blocks.map((block) => block.id)).toEqual(['periodic1', 'expr1', 'expr2', 'set1', 'expr3', 'if1', 'latch1', 'timer1', 'edge1', 'latch2', 'switch1', 'for1', 'expr4'])
  })

  it('keeps names of the variables the frames carry', () => {
    expect(recovered.project.objects.map((node) => node.name)).toEqual(['count', 'sel', 'table', 'copy', 'acc'])
    expect(recovered.sections).toEqual([])
  })

  it('reads pins, enables, settings, formulas and the loop', () => {
    const byId = new Map(recovered.blocks.map((block) => [block.id, block]))
    const id = (name: string) => recovered.project.objects.find((node) => node.name === name)!.id
    expect(byId.get('expr1')).toMatchObject({ inputs: [{ root: id('count') }], enables: [{ root: 'periodic1:eno' }], outputs: [id('count')], expression: { constants: [1], code: ['in', 0, 'const', 0, '+'] } })
    expect(byId.get('expr2')!.inputs![0]).toEqual({ root: id('table'), steps: [{ kind: 'dynamic', index: { root: id('sel') } }] })
    expect(byId.get('expr3')!.expression).toEqual({ constants: [3], code: ['in', 0, 'const', 0, '>'] })
    expect(byId.get('if1')).toMatchObject({ eno: true })
    expect(byId.get('timer1')!.settings).toMatchObject({ mode: 'TON', time_base: 'MS', pt: 300 })
    expect(byId.get('switch1')!.outputs).toEqual([null, null, null])
    expect(byId.get('for1')).toMatchObject({ body: 1, settings: { k_end: 3, k_step: 1, max_turns: 10 } })
    // The loop's one block is in it: its gate is the loop's ENO.
    expect(byId.get('expr4')!.enables).toEqual([{ root: 'for1:eno' }])
  })

  it('builds the same frames from the recovered project', () => {
    const again = build(projectOf(recovered.blocks, recovered.project.objects))
    expect(errorsOf(again.diagnostics)).toEqual([])
    expect(again.steps.map((step) => hex(step.frame))).toEqual(frames.map(hex))
  })

  it('places blocks in a staircase in program order, right of what they read', () => {
    const placed = placeBlocks(recovered.blocks, size)
    expect(placed.map((block) => block.y)).toEqual([...placed.map((block) => block.y)].sort((left, right) => left - right))
    const reader = placed.find((block) => block.id === 'expr1')!
    const source = placed.find((block) => block.id === 'periodic1')!
    expect(reader.x).toBeGreaterThan(source.x)
  })
})

describe('stored code: more block shapes', () => {
  const roundTrip = (list: readonly ProgramBlock[], nodes: readonly ObjectNode[]) => {
    const first = build(projectOf(list, nodes))
    expect(errorsOf(first.diagnostics)).toEqual([])
    const frames = first.steps.map((step) => step.frame)
    const recovered = recover(frames)
    expect(errorsOf(recovered.diagnostics)).toEqual([])
    const again = build(projectOf(recovered.blocks, recovered.project.objects))
    expect(errorsOf(again.diagnostics)).toEqual([])
    expect(again.steps.map((step) => hex(step.frame))).toEqual(frames.map(hex))
    return recovered
  }
  const constant = (id: string): ProgramBlock => ({ id, type: 'EXPR', expression: { constants: [1], code: ['const', 0] } })

  it('nested loops, several enables and a signed EDGE threshold', () => {
    const recovered = roundTrip([
      { id: 'outer', type: 'FOR', body: 3, settings: { k_start: 0, k_end: 3, k_step: 1, max_turns: 10 } },
      { id: 'inner', type: 'FOR', body: 1, settings: { k_start: 0, k_end: 2, k_step: 1, max_turns: 10 } },
      constant('x'),
      constant('y'),
      { id: 'edge', type: 'EDGE', inputs: [at('temp')], enables: [at('on'), at('go')], enableMode: 'all', onError: 'continue', settings: { edge_type: 'FALLING', change_by: -5 } },
    ], [value('temp', 'I32'), value('on', 'B'), value('go', 'B')])
    expect(recovered.blocks.map((block) => block.id)).toEqual(['for1', 'for2', 'expr1', 'expr2', 'edge1'])
    // The inner loop and the block after it are in the outer loop; the inner one's block is in the inner loop.
    expect(recovered.blocks[1]!.enables).toEqual([{ root: 'for1:eno' }])
    expect(recovered.blocks[2]!.enables).toEqual([{ root: 'for2:eno' }])
    expect(recovered.blocks[3]!.enables).toEqual([{ root: 'for1:eno' }])
    expect(recovered.blocks[4]).toMatchObject({ enableMode: 'all', onError: 'continue', settings: { edge_type: 'FALLING', change_by: -5 } })
    expect(recovered.blocks[4]!.enables).toHaveLength(2)
  })

  it('a pin taken from the settings or from an input', () => {
    roundTrip([
      { id: 'set', type: 'IO_SET_LEVEL', inputs: [at('level')], dynamicInputs: [1], settings: { device_id: 0, default_io_num: 4, flags: 4, allowed_mask: '0xff', disabled_action: 'HOLD' } },
      { id: 'wired', type: 'IO_SET_LEVEL', inputs: [at('level'), at('pin')], settings: { device_id: 0, default_io_num: 4, flags: 4, allowed_mask: '0xff', disabled_action: 'HOLD' } },
    ], [value('level', 'B'), value('pin', 'U8')])
  })

  it('positions inside folders come back rooted on the child, as the compiler writes them', () => {
    const folder: ObjectNode = { kind: 'folder', id: 'gains', name: 'gains', children: [value('vec', 'F', [1, 2, 3], 3), value('ki', 'F')] }
    const recovered = roundTrip([
      { id: 'pick', type: 'EXPR', inputs: [{ root: 'gains', steps: [{ kind: 'index', index: 0 }, { kind: 'index', index: 2 }] }], expression: { code: ['in', 0] } },
    ], [folder])
    expect(recovered.blocks[0]!.inputs![0]).toEqual({ root: 'obj-1', steps: [{ kind: 'index', index: 2 }] })
  })

  it('keeps user objects a block drives in the tree, named', () => {
    const recovered = roundTrip([{ id: 'sum', type: 'EXPR', inputs: [at('a')], outputs: ['out'], expression: { code: ['in', 0] } }], [value('a', 'F'), value('out', 'F')])
    expect(recovered.project.objects.map((node) => node.name)).toEqual(['a', 'out'])
    expect(recovered.sections).toEqual([])
    expect(recovered.blocks[0]!.outputs).toEqual(['obj-1'])
  })
})
