import { describe, expect, it } from 'vitest'
import enums from '@data-structures/enums.json'
import golden from './fixtures/vm-golden.json'
import { blockDescriptor, BLOCKS, encodeState } from '../../src/backend/vm/blocks'
import { Program } from '../../src/backend/vm/program'
import { parseTelemetryFrame, TelemetryMirror } from '../../src/backend/vm/telemetry'
import { decodeValue, encodeValue, EXEC, FLAG, KEY_VM, NO_ID, OP, T } from '../../src/backend/vm/wire'
import type { ObjType } from '../../src/backend/vm/wire'

const hex = (bytes: Uint8Array) => Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('')
const members = (name: string) => Object.fromEntries((enums.enums as Record<string, { members: { name: string; value: number }[] }>)[name].members.map((m) => [m.name, m.value]))

describe('constants follow the firmware (enums.json)', () => {
  it('exec commands', () => {
    const e = members('vm_exec_command_e')
    expect(Object.values(e).sort((a, b) => a - b)).toEqual(Object.values(EXEC).sort((a, b) => a - b))
    expect([e.VM_EXEC_SCAN_MODE, e.VM_EXEC_ONCE, e.VM_EXEC_NORMAL_MODE, e.VM_EXEC_RETAIN_CLEAR]).toEqual([EXEC.scan, EXEC.once, EXEC.normal, EXEC.retainClear])
  })

  it('object types', () => {
    const t = members('vm_obj_t_e')
    expect(Object.values(t).filter((v) => v > 0).sort((a, b) => a - b)).toEqual(Object.values(T).sort((a, b) => a - b))
  })

  it('the router key and ops', () => {
    expect(members('router_key').ROUTER_KEY_VM).toBe(KEY_VM)
    expect(Object.values(OP)).toEqual([1, 2, 3, 4, 5, 6, 7, 8, 9])
  })
})

describe('values', () => {
  it('round-trip every type, one element and several', () => {
    const cases: [ObjType, number | boolean | number[]][] = [[T.u8, 200], [T.u32, 4000000000], [T.i32, -123456], [T.f32, 1.5], [T.bool, true], [T.ptr, 513], [T.i32, [1, -2, 3]]]
    for (const [type, value] of cases) expect(decodeValue(type, encodeValue(type, value))).toEqual(value)
  })

  it('are little-endian', () => {
    expect(hex(encodeValue(T.u32, 0x01020304))).toBe('04030201')
    expect(hex(encodeValue(T.f32, 1))).toBe('0000803f')
  })
})

describe('Program frames equal the host suite builder (vm_lib.Prog), which the board accepts', () => {
  const build = () => {
    const p = new Program(20000)
    const a = p.f32(1.5, { name: 'a' })
    const b = p.u32(7, { name: 'b' })
    p.i32(-3)
    p.obj(T.u8, 5, { name: 'bytes', init: [1, 2, 3, 4, 5] })
    const flag = p.bool(true, { name: 'flag', flags: FLAG.mutable | FLAG.retentive })
    p.obj(T.f32, 150, { name: 'big', init: Array.from({ length: 150 }, (_, i) => i * 0.5) })
    const folder = p.ptr(2, [a, b], { name: 'folder' })
    const q = p.f32(undefined, { name: 'q' })
    const eno = p.bool()
    const accMember = p.acc(folder, [{ kind: 'index', index: 1 }])
    const accName = p.acc(folder, [{ kind: 'name', name: 'x' }, { kind: 'index', index: 2 }])
    const accRef = p.acc(folder, [{ kind: 'ref', via: accMember }])
    p.block({ type: golden.blocks.EXPR, inputs: [a, b, null], outputs: [q], enables: [flag], state: Uint8Array.from({ length: 12 }, (_, i) => i), eno, enableMode: 1, onError: 0 })
    p.block({ type: golden.blocks.IF, inputs: [accMember, accName] })
    p.block({ type: golden.blocks.EXPR, inputs: [accRef], outputs: [q], state: new Uint8Array(40) })
    for (let i = 0; i < 30; i++) p.u8(i, { name: `o${i}` })
    return p
  }

  it('every load frame, byte for byte', () => {
    const frames = build().frames().map((f) => ({ op: f.op, payload: hex(f.payload) }))
    expect(frames.length).toBe(golden.frames.length)
    golden.frames.forEach((want, i) => expect(frames[i], `frame ${i}`).toEqual(want))
  })

  it('frames stay within the 512-byte router frame', () => {
    for (const f of build().frames()) expect(f.payload.length + 4).toBeLessThanOrEqual(512)
  })

  it('refuses a long name and an unknown object', () => {
    expect(() => new Program().obj(T.u8, 1, { name: 'x'.repeat(16) })).toThrow(RangeError)
    expect(() => new Program().val(3, 1)).toThrow(RangeError)
  })

  it('a pin that is null is "no accessor" and one object gets one accessor', () => {
    const p = new Program()
    const a = p.f32(0)
    p.block({ type: 1, inputs: [a, a, null] })
    const add = p.frames().find((f) => f.op === OP.addBlock)!.payload
    const view = new DataView(add.buffer, add.byteOffset)
    expect([view.getUint16(14, true), view.getUint16(16, true), view.getUint16(18, true)]).toEqual([0, 0, NO_ID])
  })
})

describe('telemetry', () => {
  const describeRecord = (id: number, size: number, type: number, name: string) =>
    Uint8Array.from([id & 255, id >> 8, size & 255, size >> 8, type | (name.length << 4), FLAG.mutable, ...new TextEncoder().encode(name)])
  const valueRecord = (id: number, start: number, data: number[]) => Uint8Array.from([id & 255, id >> 8, start & 255, start >> 8, data.length & 255, data.length >> 8, ...data])
  const frame = (op: number, ...records: Uint8Array[]) => Uint8Array.from([KEY_VM, op, records.length, ...records.flatMap((r) => [...r])])

  it('parses describe and value frames, and refuses anything else', () => {
    expect(parseTelemetryFrame(frame(OP.setData, valueRecord(3, 0, [1, 2])))).toEqual([{ kind: 'value', id: 3, start: 0, data: Uint8Array.of(1, 2) }])
    expect(parseTelemetryFrame(frame(OP.addObjects, describeRecord(0x8001, 8, T.f32, 'hp')))).toEqual([{ kind: 'describe', id: 0x8001, size: 8, type: T.f32, flags: FLAG.mutable, name: 'hp' }])
    expect(parseTelemetryFrame(Uint8Array.of(KEY_VM, OP.exec, 0))).toBeUndefined()
    expect(parseTelemetryFrame(Uint8Array.of(KEY_VM, OP.setData, 1, 3, 0))).toBeUndefined()
    const cut = frame(OP.setData, valueRecord(3, 0, [1, 2]))
    expect(parseTelemetryFrame(cut.subarray(0, cut.length - 1))).toBeUndefined()
    expect(parseTelemetryFrame(Uint8Array.from([...cut, 0]))).toBeUndefined()
  })

  it('the mirror folds values, reassembles a split object and decodes by type', () => {
    const kinds = new Map([[1, { type: T.f32 as ObjType, count: 1 }], [2, { type: T.u8 as ObjType, count: 6 }], [3, { type: T.bool as ObjType, count: 1 }]])
    const mirror = new TelemetryMirror((id) => kinds.get(id))
    mirror.apply(frame(OP.setData, valueRecord(1, 0, [0, 0, 0xc0, 0x3f]), valueRecord(3, 0, [1])))
    expect(mirror.value(1)).toBe(1.5)
    expect(mirror.value(3)).toBe(true)
    mirror.apply(frame(OP.setData, valueRecord(2, 3, [4, 5, 6])))
    mirror.apply(frame(OP.setData, valueRecord(2, 0, [1, 2, 3])))
    expect(mirror.value(2)).toEqual([1, 2, 3, 4, 5, 6])
    mirror.apply(frame(OP.setData, valueRecord(1, 0, [0, 0, 0x80, 0x3f]))) // a change replaces the value
    expect(mirror.value(1)).toBe(1)
    expect(mirror.value(99)).toBeUndefined()
  })

  it('a heap object describes itself before its values', () => {
    const mirror = new TelemetryMirror(() => undefined)
    mirror.apply(frame(OP.addObjects, describeRecord(0x8001, 8, T.i32, 'buf')))
    mirror.apply(frame(OP.setData, valueRecord(0x8001, 0, [1, 0, 0, 0, 0xfe, 0xff, 0xff, 0xff])))
    expect(mirror.kind(0x8001)).toEqual({ type: T.i32, count: 2 })
    expect(mirror.value(0x8001)).toEqual([1, -2])
  })

  it('counts frames it cannot read, and waitFor resolves on the next frame or times out', async () => {
    const mirror = new TelemetryMirror(() => ({ type: T.u8 as ObjType, count: 1 }))
    mirror.apply(Uint8Array.of(0, 0, 0))
    expect(mirror.malformed).toBe(1)
    const waiting = mirror.waitFor(() => mirror.has(5), 1000)
    mirror.apply(frame(OP.setData, valueRecord(5, 0, [9])))
    await waiting
    await expect(mirror.waitFor(() => mirror.has(6), 20)).rejects.toThrow('No telemetry')
  })
})

describe('block palette from the generated descriptors', () => {
  it('has every block with its state layout', () => {
    expect(BLOCKS.size).toBe(23)
    expect(blockDescriptor('EXPR').id).toBe(blockDescriptor('VM_BLK_EXPR').id)
    expect(blockDescriptor(5).name).toBe('VM_BLK_FOR')
    expect(() => blockDescriptor('NOPE')).toThrow(RangeError)
    for (const block of BLOCKS.values()) expect(block.state?.size ?? 0).toBe(block.shape.state_len)
  })

  it('encodes a state from field names, enum members by name, padding zero', () => {
    const timer = blockDescriptor('TIMER')
    const state = encodeState(timer, { mode: 1, time_base: 'VM_TIMER_UNIT_MS', pt: 500 })
    const view = new DataView(state.buffer)
    expect(state.length).toBe(timer.state!.size)
    expect(state[0]).toBe(1)
    expect(view.getUint32(8, true)).toBe(500)
    expect(state[3]).toBe(0)
    const unit = members('vm_timer_unit_e')
    expect(state[2]).toBe(unit.VM_TIMER_UNIT_MS)
  })

  it('an enum member is found by its C name or its alias, never by guessing from the ending', () => {
    const timer = blockDescriptor('TIMER')
    const ms = (enums.enums as Record<string, { members: { name: string; alias: string; value: number }[] }>).vm_timer_unit_e.members.find((m) => m.name === 'VM_TIMER_UNIT_MS')!
    expect(encodeState(timer, { time_base: ms.alias })[2]).toBe(ms.value)
    expect(() => encodeState(timer, { time_base: 'MS' })).toThrow('not a member')
  })

  it('refuses a field that does not exist, a run-time field and an unknown enum member', () => {
    const timer = blockDescriptor('TIMER')
    expect(() => encodeState(timer, { nope: 1 })).toThrow('no state field')
    expect(() => encodeState(timer, { elapsed: 1 })).toThrow('not set by the app')
    expect(() => encodeState(timer, { time_base: 'FORTNIGHT' })).toThrow("not a member")
  })

  it('a FOR\'s span is derived: it is given as bytes', () => {
    const state = encodeState(blockDescriptor('FOR'), { span: Uint8Array.of(1, 0, 4, 0), k_end: 10 })
    expect(Array.from(state.subarray(0, 4))).toEqual([1, 0, 4, 0])
    expect(new DataView(state.buffer).getFloat32(8, true)).toBe(10)
  })
})
