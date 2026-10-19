import { describe, expect, it } from 'vitest'
import { loadWireDefinitions, wireDefinitions } from '@engine/generated'
import {
  arenaUsage,
  checkProgram,
  decodeValue,
  DEFAULT_LIMITS,
  encodeValue,
  execFrame,
  ID_NONE,
  loadFrames,
  speedFrame,
  subscribeFrame,
  toHex,
} from '@engine/wire'
import type { AccessorDef, BlockDef, DataDef, ObjectDef, ObjectFlags, ProgramIR } from '@engine/wire'
import golden from '../fixtures/vm-golden.json'

const defs = loadWireDefinitions()

describe('wire numbers come from the generated descriptors', () => {
  it('match the firmware (VM_CORE.MD op table, vm_obj.h, vm_block.h)', () => {
    expect(defs.routerKey).toBe(0x0b)
    expect(defs.ops).toEqual({ reset: 1, open: 2, addObjects: 3, setData: 4, addAccessors: 5, addBlock: 6, subscribe: 7, exec: 8, speed: 9 })
    expect(defs.objTypes).toEqual({ ptr: 1, u8: 2, u32: 3, i32: 4, f32: 5, bool: 6, str: 7 })
    expect(defs.enMode).toEqual({ any: 0, all: 1 })
    expect(defs.onError).toEqual({ stop: 0, continue: 1 })
    expect(defs.idxKinds).toEqual({ literal: 0, ref: 1, name: 2 })
    expect(defs.headFlags).toEqual({ mutable: 1, updResetable: 4, retentive: 16, usrProtected: 64 })
    expect(defs.exec.scan).toBe(0)
    expect(defs.exec.retainClear).toBe(10)
  })

  it('refuse a symbol the firmware no longer publishes', () => {
    expect(() => wireDefinitions({}, { commands: [] })).toThrow(/no enum 'router_key'/)
  })
})

describe('values', () => {
  it('round-trip every type, one element and several', () => {
    const cases: [Parameters<typeof encodeValue>[0], number | boolean | number[]][] = [
      ['u8', 200],
      ['u32', 4000000000],
      ['i32', -123456],
      ['f32', 1.5],
      ['bool', true],
      ['ptr', 513],
      ['i32', [1, -2, 3]],
    ]
    for (const [type, value] of cases) expect(decodeValue(type, encodeValue(type, value))).toEqual(value)
  })

  it('are little-endian', () => {
    expect(toHex(encodeValue('u32', 0x01020304))).toBe('04030201')
    expect(toHex(encodeValue('f32', 1))).toBe('0000803f')
  })
})

/* The program of the firmware host suite (vm_lib.Prog) whose frames the board accepts: tests/engine/fixtures/vm-golden.json. */
const build = (): ProgramIR => {
  const objects: ObjectDef[] = []
  const data: DataDef[] = []
  const accessors: AccessorDef[] = []
  const blocks: BlockDef[] = []
  const standard: ObjectFlags = { mutable: true, updResetable: true }
  const obj = (type: ObjectDef['type'], count: number, name: string, flags: ObjectFlags, init?: number | boolean | number[]): number => {
    const id = objects.length
    objects.push({ id, type, count, flags, name })
    if (init !== undefined) data.push({ id, start: 0, data: encodeValue(type, init) })
    return id
  }
  const acc = (root: number, steps: AccessorDef['steps']): number => {
    accessors.push({ id: accessors.length, root, steps })
    return accessors.length - 1
  }
  const whole = new Map<number, number>()
  const pin = (id: number | null): number => {
    if (id === null) return ID_NONE
    let slot = whole.get(id)
    if (slot === undefined) whole.set(id, (slot = acc(id, [])))
    return slot
  }

  const a = obj('f32', 1, 'a', standard, 1.5)
  const b = obj('u32', 1, 'b', standard, 7)
  obj('i32', 1, '', standard, -3)
  obj('u8', 5, 'bytes', standard, [1, 2, 3, 4, 5])
  const flag = obj('bool', 1, 'flag', { mutable: true, retentive: true }, true)
  obj('f32', 150, 'big', standard, Array.from({ length: 150 }, (_, i) => i * 0.5))
  const folder = obj('ptr', 2, 'folder', standard, [a, b])
  const q = obj('f32', 1, 'q', standard)
  const eno = obj('bool', 1, '', standard)
  const accMember = acc(folder, [{ kind: 'literal', value: 1 }])
  const accName = acc(folder, [{ kind: 'name', name: 'x' }, { kind: 'literal', value: 2 }])
  const accRef = acc(folder, [{ kind: 'ref', accessor: accMember }])

  const block = (def: Omit<BlockDef, 'id' | 'index' | 'enableMode' | 'onError' | 'eno'> & Partial<Pick<BlockDef, 'enableMode' | 'onError' | 'eno'>>) => {
    const id = blocks.length
    blocks.push({ id, index: id, enableMode: 'any', onError: 'continue', eno: ID_NONE, ...def })
  }
  block({ type: golden.blocks.EXPR, inputs: [pin(a), pin(b), pin(null)], outputs: [q], enables: [pin(flag)], state: Uint8Array.from({ length: 12 }, (_, i) => i), eno, enableMode: 'all', onError: 'stop' })
  block({ type: golden.blocks.IF, inputs: [accMember, accName], outputs: [], enables: [], state: new Uint8Array() })
  block({ type: golden.blocks.EXPR, inputs: [accRef], outputs: [q], enables: [], state: new Uint8Array(40) })
  for (let i = 0; i < 30; i++) obj('u8', 1, `o${i}`, standard, i)
  return { objects, data, accessors, blocks }
}

describe('load frames equal what the firmware host suite builds (and the board accepts)', () => {
  it('every frame, byte for byte', () => {
    const frames = loadFrames(defs, build(), DEFAULT_LIMITS, 20000).map((f) => ({ op: f.op, payload: toHex(f.payload) }))
    expect(frames.length).toBe(golden.frames.length)
    golden.frames.forEach((want, i) => expect(frames[i], `frame ${i}`).toEqual(want))
  })

  it('stay within the 512-byte router frame (4 bytes of header)', () => {
    for (const f of loadFrames(defs, build())) expect(f.payload.length + 4).toBeLessThanOrEqual(512)
  })

  it('open with the arena the program needs when none is given', () => {
    const ir = build()
    const [open] = loadFrames(defs, ir)
    const view = new DataView(open!.payload.buffer)
    expect([view.getUint16(0, true), view.getUint16(2, true), view.getUint16(4, true)]).toEqual([39, 6, 3])
    expect(view.getUint32(6, true)).toBe(arenaUsage(ir).total)
  })

  it('pass the load checks the board makes', () => {
    expect(checkProgram(build())).toEqual([])
  })
})

describe('arena size replays the board allocator', () => {
  const object = (id: number, count = 1, name = ''): ObjectDef => ({ id, type: 'f32', count, flags: {}, name })

  it('carves 4-aligned: registry tables first, then each object, accessor, name copy and block', () => {
    // registries: objects 1*4 = 4 (offset 4); the object 4 head + 4 payload + 1 name = 9 at offset 4 -> 13.
    expect(arenaUsage({ objects: [object(0, 1, 'a')], data: [], accessors: [], blocks: [] })).toEqual({ registries: 4, objects: 9, accessors: 0, blocks: 0, total: 13 })

    // + one accessor with a name step 'x': registries 4 + 4 = 8, the object 9 at 8 -> 17; the accessor 20 + 1*8 = 28 at 20 -> 48;
    // the name copy 'x\0' = 2 at 48 -> 50.
    const withAccessor = arenaUsage({
      objects: [object(0, 1, 'a')],
      data: [],
      accessors: [{ id: 0, root: 0, steps: [{ kind: 'name', name: 'x' }] }],
      blocks: [],
    })
    expect(withAccessor.registries).toBe(8)
    expect(withAccessor.accessors).toBe(28 + 2)
    expect(withAccessor.total).toBe(50)
  })

  it('counts a block as 16 + 4 per pin + its state', () => {
    const ir: ProgramIR = {
      objects: [object(0)],
      data: [],
      accessors: [{ id: 0, root: 0, steps: [] }],
      blocks: [{ id: 0, index: 0, type: 1, inputs: [0, ID_NONE], outputs: [0], enables: [], enableMode: 'any', onError: 'continue', state: new Uint8Array(12), eno: ID_NONE }],
    }
    expect(arenaUsage(ir).blocks).toBe(16 + 4 * 3 + 12)
  })
})

describe('load checks', () => {
  const base = (): ProgramIR => ({
    objects: [
      { id: 0, type: 'f32', count: 1, flags: { mutable: true }, name: 'x' },
      { id: 1, type: 'bool', count: 1, flags: { mutable: true }, name: '' },
    ],
    data: [],
    accessors: [{ id: 0, root: 0, steps: [] }],
    blocks: [],
  })
  const codes = (ir: ProgramIR) => checkProgram(ir).map((p) => p.code)
  const object = (ir: ProgramIR, i: number): ObjectDef => ir.objects[i]!

  it('accepts a valid program', () => expect(codes(base())).toEqual([]))

  it('refuses ids that do not fill 0..n-1 once each', () => {
    const ir = base()
    expect(codes({ ...ir, objects: [{ ...object(ir, 0), id: 5 }, object(ir, 1)] })).toContain('ERR_VM_REG_OOB')
    expect(codes({ ...ir, objects: [object(ir, 0), { ...object(ir, 1), id: 0 }] })).toEqual(expect.arrayContaining(['ERR_VM_REG_DUP', 'ERR_VM_LOAD_INCOMPLETE']))
    expect(codes({ ...ir, objects: [{ ...object(ir, 0), id: ID_NONE }, object(ir, 1)] })).toContain('ERR_VM_REG_OOB')
  })

  it('refuses a retained object without a name or a retained folder', () => {
    const ir = base()
    expect(codes({ ...ir, objects: [{ ...object(ir, 0), name: '', flags: { retentive: true } }, object(ir, 1)] })).toContain('ERR_VM_RETAIN_UNNAMED')
    expect(codes({ ...ir, objects: [{ id: 0, type: 'ptr', count: 1, flags: { retentive: true }, name: 'f' }, object(ir, 1)] })).toContain('ERR_VM_OBJ_RETENTIVE_PTR')
  })

  it('refuses initial values outside the object', () => {
    const ir = base()
    expect(codes({ ...ir, data: [{ id: 0, start: 1, data: encodeValue('f32', 1) }] })).toContain('ERR_VM_LOAD_DATA_RANGE')
    expect(codes({ ...ir, data: [{ id: 0, start: 0, data: Uint8Array.of(1, 2, 3) }] })).toContain('ERR_VM_LOAD_DATA_RANGE')
    expect(codes({ ...ir, data: [{ id: 7, start: 0, data: Uint8Array.of(1) }] })).toContain('ERR_VM_ACCESSOR_UNKNOWN_ID')
  })

  it('refuses an accessor that reads one not loaded before it, or too deep a chain', () => {
    const ir = base()
    expect(codes({ ...ir, accessors: [{ id: 0, root: 0, steps: [{ kind: 'ref', accessor: 0 }] }] })).toContain('ERR_VM_REG_OOB')
    const chain: AccessorDef[] = Array.from({ length: 10 }, (_, id) => ({ id, root: 0, steps: id === 0 ? [] : [{ kind: 'ref', accessor: id - 1 }] }))
    expect(codes({ ...ir, accessors: chain })).toContain('ERR_VM_ACCESSOR_DEPTH_EXCEEDED')
  })

  const block = (patch: Partial<BlockDef>): BlockDef => ({ id: 0, index: 0, type: 1, inputs: [], outputs: [], enables: [], enableMode: 'any', onError: 'continue', state: new Uint8Array(), eno: ID_NONE, ...patch })

  it('refuses a block that names what does not exist', () => {
    const ir = base()
    expect(codes({ ...ir, blocks: [block({ inputs: [9] })] })).toContain('ERR_VM_BLK_BAD_REF')
    expect(codes({ ...ir, blocks: [block({ outputs: [9] })] })).toContain('ERR_VM_BLK_BAD_REF')
    expect(codes({ ...ir, blocks: [block({ enables: [ID_NONE] })] })).toContain('ERR_VM_BLK_BAD_REF')
  })

  it('accepts an unwired input', () => {
    expect(codes({ ...base(), blocks: [block({ inputs: [ID_NONE, 0] })] })).toEqual([])
  })

  it('refuses an output named twice, an ENO that is not a writable bool, and an ENO that is also an output', () => {
    const ir = base()
    expect(codes({ ...ir, blocks: [block({ outputs: [0, 0] })] })).toContain('ERR_VM_BLK_OUTPUT_TAKEN')
    expect(codes({ ...ir, blocks: [block({ eno: 0 })] })).toContain('ERR_VM_BLK_BAD_REF')
    expect(codes({ ...ir, blocks: [block({ outputs: [1], eno: 1 })] })).toContain('ERR_VM_BLK_OUTPUT_TAKEN')
    expect(codes({ ...ir, blocks: [block({ eno: 1 })] })).toEqual([])
  })

  it('refuses more pins than the board takes', () => {
    expect(codes({ ...base(), blocks: [block({ inputs: Array<number>(17).fill(ID_NONE) })] })).toContain('ERR_VM_BLK_BAD_SHAPE')
  })
})

describe('control frames', () => {
  it('EXEC carries the command number', () => expect(execFrame(defs, 'once')).toEqual({ op: 8, payload: Uint8Array.of(1) }))

  it('SPEED is the slow-motion factor, and with a step the program time a stepped pass adds (little endian)', () => {
    expect(speedFrame(defs, 20)).toEqual({ op: 9, payload: Uint8Array.of(20, 0) })
    expect(speedFrame(defs, 5, 1000)).toEqual({ op: 9, payload: Uint8Array.of(5, 0, 0xe8, 0x03) })
  })

  it('SUBSCRIBE lists the ids and refuses more than the board takes', () => {
    expect(toHex(subscribeFrame(defs, [3, 258]).payload)).toBe('02' + '0300' + '0201')
    expect(() => subscribeFrame(defs, Array.from({ length: 129 }, (_, i) => i))).toThrow(/board takes 128/)
  })
})
