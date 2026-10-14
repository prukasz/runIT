import { describe, expect, it } from 'vitest'
import { checkProgram, Engine, ID_NONE, loadGeneratedInput, loadWireDefinitions } from '@engine'
import type { BlockCodec } from '@engine'
import { blk, build, eno, engine, flag, folder, num, out, project, read } from '../helpers'

const view = (bytes: Uint8Array) => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)

describe('a block of the core palette', () => {
  it('set: copies one variable into another', () => {
    const r = build(project([num('a'), num('b')], [blk('s', 'set', { inputs: [read('a'), read('b')] })]))
    expect(r.ir!.objects.map((o) => o.name)).toEqual(['a', 'b'])
    expect(r.ir!.accessors).toEqual([{ id: 0, root: 0, steps: [] }, { id: 1, root: 1, steps: [] }])
    const block = r.ir!.blocks[0]!
    expect(block).toMatchObject({ id: 0, index: 0, type: 6, inputs: [0, 1], outputs: [], enables: [], eno: ID_NONE })
    expect(block.state).toHaveLength(8) // vm_block_set_data_t
  })

  it('set: an unwired source leaves the constant in the state, at its offset', () => {
    const r = build(project([num('b')], [blk('s', 'set', { inputs: [null, read('b')], values: { value: 2.5 } })]))
    const block = r.ir!.blocks[0]!
    expect(block.inputs).toEqual([ID_NONE, 0])
    expect(view(block.state).getFloat32(0, true)).toBe(2.5)
  })

  it('if: routes by a condition; its outputs are objects of its own, bool, after the user objects', () => {
    const r = build(
      project(
        [flag('cond'), num('a'), num('b'), num('x'), num('y')],
        [
          blk('if1', 'if', { inputs: [read('cond')] }),
          blk('s1', 'set', { inputs: [read('a'), read('x')], enables: [out('if1', 0)] }),
          blk('s2', 'set', { inputs: [read('b'), read('y')], enables: [out('if1', 1)] }),
        ],
      ),
    )
    expect(r.order).toEqual(['if1', 's1', 's2'])
    expect(r.ids.outputs.get('if1:out0')).toBe(5)
    expect(r.ids.outputs.get('if1:out1')).toBe(6)
    expect(r.ir!.objects.slice(5).map((o) => [o.type, o.flags.usrProtected, o.name])).toEqual([['bool', true, ''], ['bool', true, '']])
    const [first, second, third] = r.ir!.blocks
    expect(first).toMatchObject({ type: 3, outputs: [5, 6], state: new Uint8Array() })
    // the enables of the two branches are accessors on the two outputs
    const acc = (id: number) => r.ir!.accessors[id]!
    expect(acc(second!.enables[0]!).root).toBe(5)
    expect(acc(third!.enables[0]!).root).toBe(6)
  })

  it('an output the user named writes that variable, and what reads the pin reads it', () => {
    const r = build(
      project(
        [num('a'), num('result'), num('copy')],
        [
          blk('s1', 'set', { inputs: [read('a'), read('result')] }),
          blk('for1', 'for', { outputs: [{ path: { root: 'result', steps: [] } }] }),
          blk('s2', 'set', { inputs: [out('for1', 0), read('copy')], enables: [eno('for1')] }),
        ],
      ),
    )
    expect(r.ids.outputs.get('for1:out0')).toBe(r.ids.variables.get('result'))
  })

  it('for: owns the run of blocks it gates, and says so in its state', () => {
    const r = build(
      project(
        [num('a'), num('b')],
        [
          blk('for1', 'for', { values: { start: 0, end: 3, step: 1, max_turns: 100 } }),
          blk('s1', 'set', { inputs: [read('a'), read('b')], enables: [eno('for1')] }),
        ],
      ),
    )
    expect(r.order).toEqual(['for1', 's1'])
    const loop = r.ir!.blocks[0]!
    const state = view(loop.state)
    expect(loop.state).toHaveLength(24)
    expect([state.getUint16(0, true), state.getUint16(2, true)]).toEqual([1, 2]) // the span [start, end)
    expect([state.getFloat32(4, true), state.getFloat32(8, true), state.getFloat32(12, true)]).toEqual([0, 3, 1])
    expect(state.getUint16(16, true)).toBe(100)
    expect(loop.eno).not.toBe(ID_NONE) // something reads its enable-out
    expect(r.ir!.objects[loop.eno]).toMatchObject({ type: 'bool', flags: { mutable: true } })
  })
})

describe('a device block', () => {
  it('io set_level: the device, the operation and the constants of its inputs', () => {
    const r = build(project([], [blk('io1', 'io', { variant: 'set_level', device: 5, values: { pin: 3, level: 1 } })]))
    const block = r.ir!.blocks[0]!
    const state = view(block.state)
    expect(block).toMatchObject({ type: 32, inputs: [], outputs: [] })
    expect(block.state).toHaveLength(56)
    expect([state.getUint16(0, true), state.getUint16(2, true), state.getUint16(4, true)]).toEqual([5, 0x0100, 0]) // device, IO_OP_SET_LEVEL (contract 1 << 8 | 0), no idle op
    expect([state.getInt32(8, true), state.getInt32(12, true)]).toEqual([3, 1]) // k[0] = pin, k[1] = level
  })

  it('a wired argument takes the slot of its place in the command, not of its place among the wired', () => {
    const r = build(project([flag('lv')], [blk('io1', 'io', { variant: 'set_level', device: 5, values: { pin: 3 }, inputs: [null, read('lv')] })]))
    expect(r.ir!.blocks[0]!.inputs).toEqual([ID_NONE, 0])
  })

  it('a read has an output object once something reads it (or the user names a variable for it); a setting has none', () => {
    const read1 = blk('io1', 'io', { variant: 'get_level', device: 5, values: { pin: 2 } })
    expect(build(project([], [read1])).ir!.blocks[0]!.outputs).toEqual([]) // nobody wants the result

    const used = build(project([num('b')], [read1, blk('s', 'set', { inputs: [out('io1', 0), read('b')] })]))
    expect(used.ir!.blocks[0]!.outputs).toHaveLength(1)
    expect(used.ir!.objects[used.ir!.blocks[0]!.outputs[0]!]!.type).toBe('i32') // an int pin
    expect(view(used.ir!.blocks[0]!.state).getUint16(2, true)).toBe(0x0102) // IO_OP_GET_LEVEL

    const named = build(project([num('level')], [{ ...read1, outputs: [{ path: { root: 'level', steps: [] } }] }]))
    expect(named.ir!.blocks[0]!.outputs).toEqual([named.ids.variables.get('level')])
  })

  it('a scaled argument keeps its constant as a float of the shown unit', () => {
    const r = build(project([], [blk('hb', 'hbridge', { variant: 'set_drive', device: 2, values: { channel: 0, drive: 50 } })]))
    const state = view(r.ir!.blocks[0]!.state)
    const bits = new DataView(new ArrayBuffer(4))
    bits.setFloat32(0, 50, true)
    expect(state.getInt32(8 + 4, true)).toBe(bits.getInt32(0, true))
  })
})

describe('what the compiler refuses, and says', () => {
  const codes = (p: ReturnType<typeof project>) => engine.compile(p).diagnostics.map((d) => d.code)
  const compiled = (p: ReturnType<typeof project>) => engine.compile(p)

  it('an unwired required pin', () => {
    const r = compiled(project([num('a')], [blk('s', 'set', { inputs: [read('a')] })]))
    expect(r.ok).toBe(false)
    expect(r.diagnostics).toContainEqual(expect.objectContaining({ code: 'pin-unwired', where: { kind: 'block', id: 's', pin: { side: 'in', index: 1 } } }))
    expect(r.frames).toEqual([])
  })

  it('a device block with no device, an unknown block, an unknown variant', () => {
    expect(codes(project([], [blk('io1', 'io', { variant: 'set_level', values: { pin: 1 } })]))).toContain('device-missing')
    expect(codes(project([], [blk('x', 'nope')]))).toContain('unknown-block')
    expect(codes(project([], [blk('io1', 'io', { variant: 'fly', device: 1 })]))).toContain('unknown-variant')
  })

  it('a path that names nothing, with the firmware tag', () => {
    expect(codes(project([num('a')], [blk('s', 'set', { inputs: [read('zzz'), read('a')] })]))).toContain('ERR_VM_ACCESSOR_UNKNOWN_ID')
  })

  it('a folder where a number is read, a constant where a value is written', () => {
    expect(codes(project([folder('f', [num('x')])], [blk('i', 'if', { inputs: [read('f')] })]))).toContain('ERR_VM_ACCESSOR_TYPE_MISMATCH')
    expect(codes(project([num('a'), num('k', 'k', { writable: false })], [blk('s', 'set', { inputs: [read('a'), read('k')] })]))).toContain('ERR_VM_OBJ_NOT_MUTABLE')
  })

  it('a block that needs its enable and has none', () => {
    expect(codes(project([], [blk('e', 'edge')]))).toContain('enable-required')
    expect(codes(project([flag('f')], [blk('e', 'edge', { enables: [read('f')] })]))).not.toContain('enable-required')
  })

  it('a field outside what the descriptor allows', () => {
    expect(codes(project([], [blk('f', 'for', { values: { op: 9 } })]))).toContain('field-invalid')
  })

  it('a block whose editor has no codec, and a codec added from outside that serves it', () => {
    // the same palette with the expression block naming an editor nobody provides
    const input = loadGeneratedInput()
    const definitions = { wire: loadWireDefinitions(), input: { ...input, blocks: input.blocks.map((b) => (b.id === 'expr' ? { ...b, editor: 'mystery' } : b)) } }
    const p = project([num('a')], [blk('e', 'expr', { groups: { inputs: 1 }, inputs: [read('a')] })])

    const without = new Engine({ definitions }).compile(p)
    expect(without.diagnostics.map((d) => d.code)).toContain('codec-missing')

    const mystery: BlockCodec = { editor: 'mystery', encode: () => Uint8Array.of(4, 0, 0, 0) }
    const extended = new Engine({ definitions, codecs: [mystery] }).compile(p)
    expect(extended.diagnostics).toEqual([])
    expect([...extended.ir!.blocks[0]!.state]).toEqual([4, 0, 0, 0])
  })

  it('an error in the variables stops the program', () => {
    expect(codes(project([num('a'), num('b', 'a')], []))).toContain('duplicate-name')
  })
})

describe('accessors', () => {
  const tree = () => [
    folder('angles', [num('arm1'), num('arm2'), num('arm3')]),
    num('table', 'table', { type: 'u8', count: 4 }),
    num('idx'),
    num('b'),
  ]
  const accessorOf = (r: ReturnType<typeof build>, block = 0, pin = 0) => r.ir!.accessors[r.ir!.blocks[block]!.inputs[pin]!]!

  it('a fixed entry of a folder becomes the root: the walk is the same every time', () => {
    const r = build(project(tree(), [blk('s', 'set', { inputs: [read('angles', { kind: 'name', name: 'arm2' }), read('b')] })]))
    expect(accessorOf(r)).toMatchObject({ root: r.ids.variables.get('arm2'), steps: [] })
    const byIndex = build(project(tree(), [blk('s', 'set', { inputs: [read('angles', { kind: 'index', index: 2 }), read('b')] })]))
    expect(accessorOf(byIndex).root).toBe(byIndex.ids.variables.get('arm3'))
  })

  it('an element of an array stays a step on the array', () => {
    const r = build(project(tree(), [blk('s', 'set', { inputs: [read('table', { kind: 'index', index: 3 }), read('b')] })]))
    expect(accessorOf(r)).toEqual({ id: expect.any(Number), root: r.ids.variables.get('table'), steps: [{ kind: 'literal', value: 3 }] })
  })

  it('a position read from another variable is an accessor of its own, made first', () => {
    const r = build(project(tree(), [blk('s', 'set', { inputs: [read('table', { kind: 'dynamic', path: { root: 'idx', steps: [] } }), read('b')] })]))
    const outer = accessorOf(r)
    expect(outer.steps).toEqual([{ kind: 'ref', accessor: expect.any(Number) }])
    const step = outer.steps[0]!
    if (step.kind !== 'ref') throw new Error('expected a reference step')
    expect(step.accessor).toBeLessThan(outer.id) // `vm_loader` needs the accessor it reads to be loaded already
    expect(r.ir!.accessors[step.accessor]).toMatchObject({ root: r.ids.variables.get('idx'), steps: [] })
  })

  it('identical paths share one accessor', () => {
    const r = build(
      project(tree(), [
        blk('s1', 'set', { inputs: [read('b'), read('idx')] }),
        blk('s2', 'set', { inputs: [read('b'), read('idx')] }),
      ]),
    )
    expect(r.ir!.accessors).toHaveLength(2)
    expect(r.ir!.blocks[0]!.inputs).toEqual(r.ir!.blocks[1]!.inputs)
  })

  it('a folder a block can re-link (a cell pin) keeps its steps, for every path through it', () => {
    const r = build(
      project(
        [folder('slots', [num('one'), num('two')]), num('source'), num('b')],
        [
          blk('c', 'clone', { inputs: [read('source'), read('slots', { kind: 'index', index: 1 })] }),
          blk('s', 'set', { inputs: [read('slots', { kind: 'index', index: 0 }), read('b')] }),
        ],
      ),
    )
    const folderId = r.ids.variables.get('slots')
    expect(r.ir!.accessors[r.ir!.blocks[0]!.inputs[1]!]).toMatchObject({ root: folderId, steps: [{ kind: 'literal', value: 1 }] }) // the cell itself
    expect(r.ir!.accessors[r.ir!.blocks[1]!.inputs[0]!]).toMatchObject({ root: folderId, steps: [{ kind: 'literal', value: 0 }] }) // not folded: it may be re-linked
  })

  it('a cell pin needs an entry of a folder', () => {
    expect(engine.compile(project([num('source'), num('b')], [blk('c', 'clone', { inputs: [read('source'), read('b')] })])).diagnostics.map((d) => d.code)).toContain('cell-needs-folder-entry')
  })
})

describe('the program that comes out', () => {
  const sample = () =>
    project(
      [flag('cond'), num('a'), num('b')],
      [blk('if1', 'if', { inputs: [read('cond')] }), blk('s', 'set', { inputs: [read('a'), read('b')], enables: [out('if1', 0)] })],
    )

  it('passes the checks the board makes at load', () => {
    expect(checkProgram(build(sample()).ir!)).toEqual([])
  })

  it('is the same every time', () => {
    const hex = (frames: ReturnType<typeof build>['frames']) => frames.map((f) => [f.op, [...f.payload]])
    expect(hex(build(sample()).frames)).toEqual(hex(build(sample()).frames))
  })

  it('opens with the slot counts and the arena it needs, then loads objects, accessors, blocks in order', () => {
    const r = build(sample())
    const [open, objects, ...rest] = r.frames
    const v = view(open!.payload)
    expect([v.getUint16(0, true), v.getUint16(2, true), v.getUint16(4, true)]).toEqual([r.ir!.objects.length, r.ir!.accessors.length, r.ir!.blocks.length])
    expect(v.getUint32(6, true)).toBe(r.arena!.total)
    expect(open!.op).toBe(engine.wire.ops.open)
    expect(objects!.op).toBe(engine.wire.ops.addObjects)
    expect(rest.filter((f) => f.op === engine.wire.ops.addBlock)).toHaveLength(2)
    expect(rest.at(-1)!.op).toBe(engine.wire.ops.addBlock)
  })

  it('reports every variable and block with the id it has on the board', () => {
    const r = build(sample())
    expect(r.ids.variables.get('cond')).toBe(0)
    expect(r.ids.blocks.get('if1')).toBe(0)
    expect(r.ids.blocks.get('s')).toBe(1)
  })
})
