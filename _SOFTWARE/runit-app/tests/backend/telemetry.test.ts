import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import { RunitLink } from '@backend'
import type { TelemetryUpdate } from '@engine'
import { TelemetryReader, toHex, writeFrames } from '@engine'
import { blk, build, engine, num, project, read } from '../engine/helpers'
import { defs, FakeBoard, until } from './fakes'

/* The program the captured frames come from (see fixtures/board-streams.json): z = x * 2 + y, y starting at 3. */
const program = () =>
  build(
    project(
      [num('x'), num('y', 'y', { initial: 3 }), num('z')],
      [blk('e', 'expr', { groups: { inputs: 2 }, inputs: [read('x'), read('y')], outputs: [{ path: { root: 'z', steps: [] } }], editor: { text: 'in0 * 2 + in1' } })],
    ),
  )

const fixture = JSON.parse(readFileSync(fileURLToPath(new URL('./fixtures/board-streams.json', import.meta.url)), 'utf8')) as { frames: { stream: number; hex: string }[] }
const bytes = (hex: string) => Uint8Array.from(hex.match(/../g)!.map((h) => Number.parseInt(h, 16)))
const telemetry = fixture.frames.filter((f) => f.stream === defs.streams.telemetry).map((f) => bytes(f.hex))

describe('writing a variable at run time (the remote control)', () => {
  it('makes the SET_DATA frame the board accepted: id, element, length, the value', () => {
    const result = program()
    const [frame, ...more] = engine.write(result, [{ variable: 'x', value: 5 }])
    expect(more).toEqual([])
    expect(frame!.op).toBe(engine.wire.ops.setData)
    expect(toHex(frame!.payload)).toBe('010000000004000000a040') // count 1, object 0, element 0, 4 bytes, 5.0f
  })

  it('writes one element of an array, a list, and text', () => {
    const result = build(project([num('a', 'a', { count: 4 }), { kind: 'value', id: 't', name: 't', type: 'str', count: 8 }], []))
    const element = engine.write(result, [{ variable: 'a', value: 2.5, element: 3 }])[0]!
    expect(toHex(element.payload)).toBe('0100000300040000002040') // object 0, element 3, 2.5f
    const list = engine.write(result, [{ variable: 'a', value: [1, 2] }])[0]!
    expect(list.payload).toHaveLength(1 + 6 + 8)
    expect(toHex(engine.write(result, [{ variable: 't', value: new TextEncoder().encode('hi') }])[0]!.payload)).toBe('010100000002006869')
  })

  it('cuts a long value between elements and splits the frames to fit the board', () => {
    const result = build(project([num('a', 'a', { count: 200 })], []))
    const frames = engine.write(result, [{ variable: 'a', value: Array.from({ length: 200 }, (_, i) => i) }])
    expect(frames.length).toBeGreaterThan(1)
    const records = frames.flatMap((f) => {
      const view = new DataView(f.payload.buffer)
      const out: { start: number; length: number }[] = []
      for (let at = 1, n = 0; n < f.payload[0]!; n++) {
        const length = view.getUint16(at + 4, true)
        out.push({ start: view.getUint16(at + 2, true), length })
        at += 6 + length
      }
      return out
    })
    expect(records.reduce((sum, r) => sum + r.length, 0)).toBe(800)
    expect(records.every((r) => r.length % 4 === 0)).toBe(true) // whole floats only
    let next = 0
    for (const r of records) {
      expect(r.start).toBe(next)
      next += r.length / 4
    }
  })

  it('refuses a variable the program does not have', () => {
    expect(() => engine.write(program(), [{ variable: 'nope', value: 1 }])).toThrow(/not a variable/)
    expect(() => writeFrames(engine.wire, [{ id: 99, start: 0, data: Uint8Array.of(1) }], new Map())).toThrow(RangeError)
  })
})

describe('reading telemetry (what the board sent, from a real board)', () => {
  it('after SUBSCRIBE: everything once; after each runtime write: what changed, by variable', () => {
    const reader = engine.telemetry(program())
    const updates = telemetry.map((f) => reader.read(f)!)
    const shape = (u: TelemetryUpdate) => u.values.map((v) => [v.variable, v.start, v.value])
    expect(updates.map(shape)).toEqual([
      [['x', 0, 0], ['y', 0, 3], ['z', 0, 0]],
      [['x', 0, 5], ['z', 0, 13]], // y did not change: not sent
      [['x', 0, 7.5], ['z', 0, 18]],
    ])
    expect(updates.every((u) => u.values.every((v) => v.type === 'f32'))).toBe(true)
  })

  it('reads a frame it was written with: the same decoder both ways', () => {
    const result = program()
    const written = engine.write(result, [{ variable: 'x', value: 5 }])[0]!
    const frame = Uint8Array.of(engine.wire.routerKey, written.op, ...written.payload) // a telemetry frame is the command without seq / flags
    expect(engine.telemetry(result).read(frame)!.values[0]).toMatchObject({ variable: 'x', value: 5 })
  })

  it('knows the type of an object the board announces before its values, and gives bytes for one it does not know', () => {
    const reader = new TelemetryReader(engine.wire)
    const name = new TextEncoder().encode('tmp')
    const announce = Uint8Array.of(engine.wire.routerKey, engine.wire.ops.addObjects, 1, 9, 0, 4, 0, engine.wire.objTypes.f32 | (name.length << 4), 0, ...name)
    const first = reader.read(announce)!
    expect(first.objects).toEqual([{ id: 9, type: 'f32', name: 'tmp', payload: 4, flags: 0 }])
    const values = Uint8Array.of(engine.wire.routerKey, engine.wire.ops.setData, 2, 9, 0, 0, 0, 4, 0, 0, 0, 0xc0, 0x3f, 7, 0, 0, 0, 1, 0, 0xaa)
    expect(reader.read(values)!.values).toEqual([
      { id: 9, start: 0, bytes: Uint8Array.of(0, 0, 0xc0, 0x3f), type: 'f32', value: 1.5 },
      { id: 7, start: 0, bytes: Uint8Array.of(0xaa), value: Uint8Array.of(0xaa) },
    ])
  })

  it('says nothing for a frame that is not telemetry, or is cut short', () => {
    const reader = engine.telemetry(program())
    expect(reader.read(Uint8Array.of(0x01, 0x04, 0x00))).toBeUndefined() // another router
    expect(reader.read(Uint8Array.of(engine.wire.routerKey, 0x7f, 0x00))).toBeUndefined() // another op
    expect(reader.read(telemetry[1]!.subarray(0, 10))).toBeUndefined()
    expect(reader.read(new Uint8Array(0))).toBeUndefined()
  })
})

describe('the streams of the link, raw', () => {
  it('telemetry from a real board arrives through the link in packets of 20 bytes and decodes by variable', async () => {
    const board = new FakeBoard()
    const link = new RunitLink(board, { reconnect: false })
    await link.connect(board.device)
    const reader = engine.telemetry(program())
    const seen: unknown[][] = []
    link.on('telemetry', (data) => seen.push(reader.read(data)!.values.map((v) => [v.variable, v.value])))
    for (const frame of telemetry) board.notify(Uint8Array.of(defs.streams.telemetry, ...frame))
    await until(() => seen.length === 3)
    expect(seen.at(-1)).toEqual([['x', 7.5], ['z', 18]])
    await link.close()
  })

  it('every stream is delivered raw, with its name and the time it arrived; errors and logs are not interpreted', async () => {
    const board = new FakeBoard()
    const link = new RunitLink(board, { reconnect: false })
    await link.connect(board.device)
    const all: { name?: string; data: number[]; at: number }[] = []
    const errors: number[][] = []
    link.onStream((f) => all.push({ name: f.name, data: [...f.data], at: f.at }))
    link.on('errors', (data) => errors.push([...data]))
    board.notify(Uint8Array.of(defs.streams.errors, 1, 2, 3), defs.gatt.logs)
    board.notify(Uint8Array.of(defs.streams.logs, 4), defs.gatt.logs)
    board.notify(Uint8Array.of(defs.streams.events, 5))
    board.notify(Uint8Array.of(0x7f, 6)) // a stream the firmware does not publish
    await until(() => all.length === 4)
    expect(all.map((f) => [f.name, f.data])).toEqual([['errors', [1, 2, 3]], ['logs', [4]], ['events', [5]], [undefined, [6]]])
    expect(errors).toEqual([[1, 2, 3]])
    expect(all.every((f) => Math.abs(f.at - Date.now()) < 1000)).toBe(true)
    await link.close()
  })
})
