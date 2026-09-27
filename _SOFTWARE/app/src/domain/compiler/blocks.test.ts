import { describe, expect, it } from 'vitest'
import { decodeVmObjectHead, runitVmCatalog } from '../descriptors'
import { createProject } from '../project'
import type { ObjectNode, ObjectPath, ProgramBlock, ProjectDocument, ValueNode } from '../project'
import { compileProgram } from '.'

const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join('')
const catalog = runitVmCatalog()
const value = (id: string, type: string, extra: Partial<ValueNode> = {}): ValueNode => ({ kind: 'value', id, name: id, type, length: 1, mutable: true, retentive: false, ...extra })
const project = (objects: readonly ObjectNode[]): ProjectDocument => ({ ...createProject('test'), objects })
const at = (root: string): ObjectPath => ({ root })
const compile = (objects: readonly ObjectNode[], blocks: readonly ProgramBlock[]) => compileProgram(project(objects), catalog, { maxFrameBytes: 244, blocks })
/** The 0x45 frames of a compile, without class and packet. */
const blockRecords = (compiled: ReturnType<typeof compile>) => compiled.frames.filter((frame) => frame[1] === catalog.packets.addBlock).map((frame) => frame.subarray(2))
const errors = (compiled: ReturnType<typeof compile>) => compiled.diagnostics.filter((entry) => entry.severity === 'error')

describe('block palette', () => {
  it('reads every block type from the descriptors', () => {
    expect(catalog.blocks.map((block) => block.key)).toEqual(['EXPR', 'EXPR_BIT', 'IF', 'SWITCH', 'FOR', 'SET', 'CLONE', 'EDGE', 'TIMER', 'IO_SET_LEVEL', 'IO_TOGGLE', 'LATCH', 'PERIODIC', 'ACTION', 'ON_EVENT'])
    expect(catalog.block('EXPR')?.encoding?.opcode('+')?.symbol).toBe('VM_EXPR_ADD')
    expect(catalog.blockPinMax).toEqual({ in: 16, out: 16, en: 16 })
  })
})

describe('compileProgram: blocks', () => {
  it.each(catalog.blocks.filter((block) => block.encoding).flatMap((block) => block.encoding!.examples.map((example) => [block.key, example] as const)))('%s example "%s"', (key, example) => {
    const inputs = ['a', 'b', 'c'].map((id) => value(id, key === 'EXPR' ? 'F' : 'U32'))
    const code = example.code.map((token) => (/^\d+$/.test(token) ? Number(token) : token))
    const compiled = compile(inputs, [{ id: 'x', type: key, inputs: inputs.map((entry) => at(entry.id)), expression: { constants: example.constants, code } }])
    expect(compiled.diagnostics).toEqual([])
    const record = blockRecords(compiled)[0]!
    expect(hex(record.subarray(14 + 2 * (3 + 1)))).toBe(example.customData)
  })

  it('builds PERIODIC → EXPR counter → IO_TOGGLE like the firmware tests', () => {
    const compiled = compile([value('count', 'F')], [
      { id: 'every', type: 'PERIODIC', settings: { period: 500, time_base: 'MS' } },
      { id: 'add', type: 'EXPR', inputs: [at('count'), at('every:q0')], outputs: ['count'], expression: { code: ['in', 0, 'in', 1, '+'] } },
      { id: 'blink', type: 'IO_TOGGLE', enables: [at('every:q0')], settings: { allowed_mask: 1 << 5, device_id: 0, default_io_num: 5 } },
    ])
    expect(compiled.diagnostics).toEqual([])
    expect(compiled.counts).toEqual({ objects: 2, accessors: 2, blocks: 3 })
    // Objects: count 0 (user, driven), every:q0 1 (B). Accessors: count 0, tick 1.
    expect(compiled.blocks.blocks.map((block) => [block.type, block.inputs, block.outputs, block.enables])).toEqual([
      ['PERIODIC', [], [1], []],
      ['EXPR', [0, 1], [0], []],
      ['IO_TOGGLE', [], [], [1]],
    ])
    const [periodic, expr] = blockRecords(compiled)
    // blk 0, label 0, type 13, 0 in, 1 out, 0 en, any, stop, 16 B state, no ENO | q 1 | period 500 ms
    expect(hex(periodic!)).toBe('00000000' + '0d' + '000100' + '0000' + '1000' + 'ffff' + '0100' + 'f4010000' + '00' + '00'.repeat(11))
    expect(hex(expr!.subarray(14))).toBe('0000' + '0100' + '0000' + '0000' + '0500' + '0100010106')
    // The driven user object: mutable, fresh only in the pass that wrote it.
    const addObjects = compiled.frames.find((frame) => frame[1] === catalog.packets.addObjects)!
    expect(decodeVmObjectHead(catalog, addObjects.subarray(5, 9))).toMatchObject({ 'f.mutable': 1, 'f.upd_resetable': 1 })
  })

  it('gives each output and the ENO a block-owned object after the user tree', () => {
    const compiled = compile([value('c', 'B')], [{ id: 'if', type: 'IF', inputs: [at('c')], eno: true }])
    expect(compiled.diagnostics).toEqual([])
    expect(compiled.objects.sections).toEqual([{ key: 'user', first: 0, count: 1 }, { key: 'blocks', first: 1, count: 3 }])
    expect([...compiled.objects.wireIdOf].slice(1)).toEqual([['if:q0', 1], ['if:q1', 2], ['if:eno', 3]])
    expect(compiled.objects.objects.slice(1).map((entry) => entry.type.key)).toEqual(['B', 'B', 'B'])
    expect(compiled.blocks.blocks[0]).toMatchObject({ outputs: [1, 2], eno: 3 })
  })

  it('allocates an ENO automatically when used as an input or enable, and shares it', () => {
    const source: ProgramBlock = { id: 'source', type: 'PERIODIC', settings: { period: 1 } }
    const unused = compile([], [source])
    expect(unused.objects.wireIdOf.has('source:eno')).toBe(false)
    const used = compile([], [
      source,
      { id: 'branch', type: 'IF', inputs: [at('source:eno')] },
      { id: 'sink', type: 'PERIODIC', settings: { period: 1 }, enables: [at('source:eno')] },
    ])
    expect(used.diagnostics).toEqual([])
    const eno = used.objects.wireIdOf.get('source:eno')!
    expect(used.blocks.blocks[0]!.eno).toBe(eno)
    expect(used.objects.objects.filter((object) => object.node.id === 'source:eno')).toHaveLength(1)
    expect(used.blocks.blocks[1]!.inputs[0]).toBe(used.blocks.blocks[2]!.enables[0])
  })

  it('uses only the constant pin in static mode and preserves all 64 mask bits in dynamic mode', () => {
    const base: ProgramBlock = { id: 'pin', type: 'IO_TOGGLE', settings: { device_id: 0, default_io_num: 5, allowed_mask: 'ignored in static mode' } }
    const fixed = compile([], [base])
    expect(fixed.diagnostics).toEqual([])
    const record = blockRecords(fixed)[0]!
    const maskOffset = catalog.wire.addBlock.recordSize
    expect(new DataView(record.buffer, record.byteOffset).getBigUint64(maskOffset, true)).toBe(1n << 5n)
    const dynamic = compile([], [{ ...base, dynamicInputs: [0], settings: { ...base.settings, allowed_mask: '0x8000000000000020' } }])
    expect(dynamic.diagnostics).toEqual([])
    const encoded = blockRecords(dynamic)[0]!
    expect(new DataView(encoded.buffer, encoded.byteOffset).getBigUint64(maskOffset, true)).toBe((1n << 63n) | (1n << 5n))
    expect(compile([], [{ ...base, dynamicInputs: [0] }]).ok).toBe(false)
    const wired = compile([value('number', 'U32')], [{ ...base, inputs: [at('number')], settings: { ...base.settings, allowed_mask: '0x20' } }])
    expect(wired.diagnostics).toEqual([])
  })

  it('writes the FOR span and nests loops', () => {
    const blocks: ProgramBlock[] = [
      { id: 'outer', type: 'FOR', body: 3, settings: { k_start: 0, k_end: 3, k_step: 1, max_turns: 10 } },
      { id: 'inner', type: 'FOR', body: 1, settings: { k_start: 0, k_end: 2, k_step: 1, max_turns: 10 } },
      { id: 'x', type: 'PERIODIC', settings: { period: 1 } },
      { id: 'y', type: 'PERIODIC', settings: { period: 1 } },
      { id: 'after', type: 'PERIODIC', settings: { period: 1 } },
    ]
    const compiled = compile([], blocks)
    expect(compiled.diagnostics).toEqual([])
    expect(compiled.blocks.blocks.map((block) => block.span)).toEqual([{ start: 1, end: 4 }, { start: 2, end: 3 }, undefined, undefined, undefined])
    expect(hex(blockRecords(compiled)[1]!.subarray(16, 20))).toBe('02000300')
    const past = compile([], [{ ...blocks[0]!, body: 1 }, { ...blocks[1]!, body: 2 }, ...blocks.slice(2)])
    expect(errors(past)).toEqual([expect.objectContaining({ blockId: 'inner', message: expect.stringContaining("loop 'outer'") })])
  })

  it('stores the EDGE threshold in the type of the signal', () => {
    const edge = (signal: ValueNode) => blockRecords(compile([signal], [{ id: 'e', type: 'EDGE', inputs: [at(signal.id)], settings: { edge_type: 'BOTH', change_by: 2 } }]))[0]!
    // 1 input + 1 output: the state starts at 14 + 4, change_by 4 bytes into it
    expect(hex(edge(value('f', 'F')).subarray(22, 26))).toBe('00000040')
    expect(hex(edge(value('n', 'U32')).subarray(22, 26))).toBe('02000000')
  })

  it.each([
    ['an unknown type', [{ id: 'b', type: 'NOPE' }], 'ERR_VM_BLK_UNKNOWN_TYPE'],
    ['an unwired required input', [{ id: 'b', type: 'IF' }], 'ERR_VM_BLK_BAD_SHAPE'],
    ['too many outputs', [{ id: 'b', type: 'IF', inputs: [at('n')], outputs: [null, null, null] }], 'ERR_VM_BLK_BAD_SHAPE'],
    ['two writers of one object', [{ id: 'b', type: 'PERIODIC', settings: { period: 1 }, outputs: ['n'] }, { id: 'c', type: 'PERIODIC', settings: { period: 1 }, outputs: ['n'] }], 'ERR_VM_BLK_OUTPUT_TAKEN'],
    ['an enum value it lacks', [{ id: 'b', type: 'PERIODIC', settings: { period: 1, time_base: 'WEEK' } }], 'ERR_VM_BLK_BAD_SHAPE'],
    ['a setting out of range', [{ id: 'b', type: 'PERIODIC', settings: { period: -1 } }], 'ERR_VM_BLK_BAD_SHAPE'],
    ['PERIODIC without a period', [{ id: 'b', type: 'PERIODIC' }], 'ERR_VM_BLK_BAD_SHAPE'],
    ['an expression leaving two values', [{ id: 'b', type: 'EXPR', inputs: [at('n')], expression: { code: ['in', 0, 'dup'] } }], 'ERR_VM_BLK_BAD_SHAPE'],
    ['an expression reading an unwired input', [{ id: 'b', type: 'EXPR', inputs: [at('n')], expression: { code: ['in', 1] } }], 'ERR_VM_BLK_BAD_SHAPE'],
    ['a folder on a value input', [{ id: 'b', type: 'IF', inputs: [at('f')] }], 'ERR_VM_ACCESSOR_TYPE_MISMATCH'],
    ['dynamic IO pins the mask does not allow', [{ id: 'b', type: 'IO_TOGGLE', dynamicInputs: [0], settings: { allowed_mask: 1, default_io_num: 5 } }], 'ERR_VM_BLK_BAD_SHAPE'],
  ] as const)('refuses %s', (_, blocks, firmwareError) => {
    const compiled = compile([value('n', 'U32'), { kind: 'folder', id: 'f', name: 'f', children: [value('m', 'U8')] }], blocks as readonly ProgramBlock[])
    expect(compiled.ok).toBe(false)
    expect(errors(compiled)).toEqual(expect.arrayContaining([expect.objectContaining({ firmwareError })]))
  })

  it('wants a folder entry for CLONE\'s cell', () => {
    const tree: ObjectNode[] = [value('src', 'F'), { kind: 'folder', id: 'box', name: 'box', children: [value('slot', 'F')] }]
    const good = compile(tree, [{ id: 'c', type: 'CLONE', inputs: [at('src'), { root: 'box', steps: [{ kind: 'index', index: 0 }] }] }])
    expect(good.diagnostics).toEqual([])
    const bad = compile(tree, [{ id: 'c', type: 'CLONE', inputs: [at('src'), at('slot')] }])
    expect(errors(bad)).toEqual([expect.objectContaining({ message: expect.stringContaining('folder entry') })])
  })

  it('adds the blocks to the arena', () => {
    const compiled = compile([], [{ id: 'p', type: 'PERIODIC', settings: { period: 1 } }])
    // objects: registry 4 + tick (4 + 1 → 8); blocks: registry 4 + 16 + 1 pointer + 16 state
    expect(compiled.arenaBytes).toBe(4 + 8 + 4 + 36)
  })
})
