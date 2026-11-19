import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import { decodeErrorRecord, decodeFrame, decodeHello, decodeResponse, encodeHello, encodeRequest, errorCatalog, fragment, HEADER_BYTES, Reassembler } from '@backend'
import { defs } from './fakes'
import { record } from './records'

const hex = (bytes: Uint8Array) => [...bytes].map((b) => b.toString(16).padStart(2, '0')).join(' ')
const bytes = (text: string) => Uint8Array.from(text.split(' ').map((b) => Number.parseInt(b, 16)))
const source = (path: string) => readFileSync(fileURLToPath(new URL(`../../../runit-esp/${path}`, import.meta.url)), 'utf8')

describe('the numbers come from the firmware', () => {
  it('the runIT UUIDs and the fragment bits equal the headers', () => {
    const ids = source('components/app/runit/runit_ids.h')
    const header = source('components/sys/bus/sys_ble/sys_ble.h')
    const define = (text: string, name: string) => Number(new RegExp(`#define\\s+${name}\\s+(0x[0-9A-Fa-f]+)`).exec(text)?.[1])
    expect(defs.gatt).toEqual({
      service: define(ids, 'RUNIT_BLE_SERVICE'),
      tx: define(ids, 'RUNIT_BLE_TX'),
      rx: define(ids, 'RUNIT_BLE_RX'),
      logs: define(ids, 'RUNIT_BLE_LOGS'),
    })
    expect(defs.frag).toEqual({ last: define(header, 'SYS_BLE_FRAG_LAST'), first: define(header, 'SYS_BLE_FRAG_FIRST'), countMask: define(header, 'SYS_BLE_FRAG_COUNT_MASK') })
  })

  it('the root keys are the module numbers the routers declare', () => {
    const declared = (path: string, name: string) => Number(new RegExp(String.raw`MODULE_ID_DECLARE\(${name},\s*(0x[0-9A-Fa-f]+)\)`).exec(source(path))?.[1])
    expect(defs.keys.sys).toBe(declared('components/sys/sys_system/sys_system_id.h', 'MODULE_ID_SYS'))
    expect(defs.keys.watch).toBe(declared('components/sys/sys_watch/sys_watch_id.h', 'MODULE_ID_WATCH'))
    expect(defs.keys.session).toBe(0x0e)
  })

  it('the session header is CMD_HEAD and the frame limit equals CMD_FRAME_MAX', () => {
    const cmd = source('components/core/cmd/cmd.h')
    expect(Number(/#define\s+CMD_FRAME_MAX\s+(\d+)u/.exec(cmd)?.[1])).toBe(defs.frameMax)
    expect(Number(/#define\s+CMD_HEAD\s+(\d+)u/.exec(cmd)?.[1])).toBe(defs.frameHead)
    expect(HEADER_BYTES).toBe(defs.frameHead)
  })
})

describe('frames of the session (SYS_SESSION.MD)', () => {
  it('encodes a request: [frame byte][seq u16][keys][payload]', () => {
    expect(hex(encodeRequest(defs, 7, { keys: [defs.keys.i2c!, 0x02], payload: Uint8Array.of(0) }))).toBe('00 07 00 02 02 00') // i2c SCAN bus 0
    expect(hex(encodeRequest(defs, 0x1234, { keys: [defs.keys.sys!, 0x01], urgent: true }))).toBe('01 34 12 28 01') // sys PING, urgent
    expect(hex(encodeRequest(defs, 7, { keys: [defs.keys.sys!, 0x01], noReply: true })).slice(0, 2)).toBe('02')
    expect(() => encodeRequest(defs, 0x10000, { keys: [1] })).toThrow(RangeError)
    expect(() => encodeRequest(defs, 0, { keys: [] })).toThrow(RangeError)
  })

  it('decodes an answer (kind 1, status in bit 2), and nothing else', () => {
    expect(decodeResponse(defs, bytes('40 07 00 10 00 00 00'))).toMatchObject({ seq: 7, ok: true })
    expect([...decodeResponse(defs, bytes('40 07 00 10 00 00 00'))!.data]).toEqual([0x10, 0, 0, 0])
    expect(decodeResponse(defs, bytes('44 07 01'))).toMatchObject({ seq: 0x107, ok: false })
    expect(decodeResponse(defs, bytes('80 07 00 04 01'))).toBeUndefined() // a PUSH frame (an error record)
    expect(decodeResponse(defs, bytes('40 07'))).toBeUndefined()
    expect(decodeFrame(defs, bytes('80 07 00 04 01'))).toMatchObject({ kind: defs.session.kind.push, seq: 7 })
  })

  it('a HELLO (sys_session_hello_t, 9 B after the op) says its sender\'s own id: the app a client id, a board its place', () => {
    const { hello } = defs.session.control
    const mine = encodeHello(defs, { id: defs.session.id.app, bootId: 0x12345678 })
    // [HELLO][id][boot_id u32][build_id u32]
    expect([...mine]).toEqual([hello, 0xf0, 0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0])
    expect(decodeHello(defs, mine)).toEqual({ id: 0xf0, bootId: 0x12345678, buildId: 0 })
    const board = Uint8Array.from([hello, 0x03, 1, 2, 3, 4, 5, 6, 7, 8])
    expect(decodeHello(defs, board)).toEqual({ id: 3, bootId: 0x04030201, buildId: 0x08070605 })
    expect(decodeHello(defs, board.subarray(0, 9))).toBeUndefined() // one byte short
    expect(decodeHello(defs, Uint8Array.of(defs.session.control.ping))).toBeUndefined()
  })

  it('decodes an error record, naming the tags from errors.json', () => {
    const request = [defs.keys.i2c!, defs.dir.read, 0x02, 0x00]
    const data = record([['ERR_ROUTER_KEY', { key: defs.keys.i2c! }], ['ERR_I2C_BUS_STUCK', { bus: 0, addr: 0 }]], request, 7, 1_766_810_422_592)
    const response = decodeResponse(defs, Uint8Array.from([0x44, 7, 0, ...data]))!
    const parsed = decodeErrorRecord(defs, response.data, errorCatalog())!
    expect(parsed.source).toBe('command')
    expect(parsed.time).toBe(1_766_810_422_592) // the board's clock, UTC ms
    expect(parsed.command).toMatchObject({ sender: 0xf0, seq: 7, flags: 0, requestCut: false })
    expect([...parsed.command!.request]).toEqual(request)
    expect(parsed.chain.map((n) => n.info?.name)).toEqual(['ERR_ROUTER_KEY', 'ERR_I2C_BUS_STUCK'])
    expect(parsed.chain[0]!.fields).toEqual({ key: defs.keys.i2c })
    expect(parsed.cause?.info?.name).toBe('ERR_I2C_BUS_STUCK')
    expect(parsed.cause?.fields).toEqual({ bus: 0, addr: 0 })
    expect(parsed.chainCut).toBe(false)
  })

  it('keeps the raw payload of a tag it does not know, and refuses a record that is cut short', () => {
    const data = bytes('00 00 00 00 00 00 00 00 00 01 78 56 34 12 01 aa')
    const record = decodeErrorRecord(defs, data, errorCatalog())!
    expect(record.source).toBe('internal')
    expect(record.cause).toMatchObject({ tag: 0x12345678, severity: 0 }) // a tag errors.json does not have: no severity
    expect(record.cause?.info).toBeUndefined()
    expect(decodeErrorRecord(defs, data.subarray(0, 15), errorCatalog())).toBeUndefined()
    expect(decodeErrorRecord(defs, new Uint8Array(0))).toBeUndefined()
  })
})

describe('framing (SYS_BLE.MD examples)', () => {
  it('a frame that fits one packet is C0 <frame>', () => {
    expect(fragment(defs, Uint8Array.of(1, 2, 3), 20).map(hex)).toEqual(['c0 01 02 03'])
  })

  it('a three-packet frame is 40, 01, 82', () => {
    const packets = fragment(defs, Uint8Array.from({ length: 7 }, (_, i) => i + 1), 4)
    expect(packets.map((p) => p[0]!.toString(16))).toEqual(['40', '1', '82'])
    expect(packets.map((p) => p.length)).toEqual([4, 4, 2])
  })

  it('the counter wraps at 64 and the frame still comes back whole', () => {
    const frame = Uint8Array.from({ length: 200 }, (_, i) => i & 0xff)
    const packets = fragment(defs, frame, 3) // 2 data bytes a packet: 100 packets
    expect(packets).toHaveLength(100)
    expect(packets[64]![0]! & 0x3f).toBe(0)
    const reassembler = new Reassembler(defs)
    const results = packets.map((p) => reassembler.push(p))
    expect(results.slice(0, -1).every((r) => r === undefined)).toBe(true)
    expect(results.at(-1)).toEqual({ frame })
  })

  it('round trips every length up to the frame limit at several packet sizes', () => {
    for (const size of [2, 3, 20, 100, 240, 512]) {
      for (const length of [1, 2, 19, 20, 21, 239, 240, 241, 511, 512]) {
        const frame = Uint8Array.from({ length }, (_, i) => (i * 7) & 0xff)
        const reassembler = new Reassembler(defs)
        const results = fragment(defs, frame, size).map((p) => reassembler.push(p))
        expect(results.at(-1), `${length} B in packets of ${size}`).toEqual({ frame })
      }
    }
  })

  it('drops the rest of a frame after a packet out of order, up to the next FIRST (as the firmware does)', () => {
    const reassembler = new Reassembler(defs)
    const [a, , c] = fragment(defs, Uint8Array.from({ length: 9 }, (_, i) => i), 4)
    expect(reassembler.push(a!)).toBeUndefined()
    expect(reassembler.push(c!)).toMatchObject({ dropped: expect.stringMatching(/where 1 was expected/) })
    expect(reassembler.push(c!)).toMatchObject({ dropped: expect.stringMatching(/outside a frame/) })
    expect(reassembler.push(fragment(defs, Uint8Array.of(9, 9), 20)[0]!)).toEqual({ frame: Uint8Array.of(9, 9) })
  })

  it('drops a frame over the limit', () => {
    const reassembler = new Reassembler(defs)
    const results = fragment(defs, new Uint8Array(defs.frameMax + 1), 100).map((p) => reassembler.push(p))
    expect(results.some((r) => r && 'dropped' in r)).toBe(true)
    expect(results.some((r) => r && 'frame' in r)).toBe(false)
  })
})
