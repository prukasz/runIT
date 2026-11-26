import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import { decodeErrorRecord, decodeFrame, decodeHello, encodeFrame, encodeHello, encodeRequest, errorCatalog, fragment, frameRequest, HELLO_BYTES, Reassembler, requestFrame } from '@backend'
import { defs } from './fakes'
import { record } from './records'

const hex = (bytes: Uint8Array) => [...bytes].map((b) => b.toString(16).padStart(2, '0')).join(' ')
const bytes = (text: string) => Uint8Array.from(text.split(' ').map((b) => Number.parseInt(b, 16)))
const source = (path: string) => readFileSync(fileURLToPath(new URL(`../../../runit-esp/${path}`, import.meta.url)), 'utf8')

describe('the numbers come from the firmware', () => {
  const define = (text: string, name: string) => Number(new RegExp(String.raw`#define\s+${name}\s+\(?(0x[0-9A-Fa-f]+|\d+)u?`).exec(text)?.[1])
  const shifted = (text: string, name: string) => {
    const m = new RegExp(String.raw`#define\s+${name}\s+\((\d+)u\s*<<\s*(\d+)\)`).exec(text)
    return m ? Number(m[1]) << Number(m[2]) : Number.NaN
  }

  it('the runIT UUIDs and the fragment bits equal the headers', () => {
    const ids = source('components/app/runit/runit_ids.h')
    const ble = source('components/sys/wireless/sys_ble/sys_ble.h')
    expect(defs.gatt).toEqual({
      service: define(ids, 'RUNIT_BLE_SERVICE'),
      session: define(ids, 'RUNIT_BLE_SESSION'),
      bridge: define(ids, 'RUNIT_BLE_BRIDGE'),
      logs: define(ids, 'RUNIT_BLE_LOGS'),
      telemetry: define(ids, 'RUNIT_BLE_TELEMETRY'),
    })
    expect(defs.frag).toEqual({ last: define(ble, 'SYS_BLE_FRAG_LAST'), first: define(ble, 'SYS_BLE_FRAG_FIRST'), countMask: define(ble, 'SYS_BLE_FRAG_COUNT_MASK') })
    expect(defs.ble.characteristic.framed).toBe(shifted(ble, 'SYS_BLE_CHR_FRAMED'))
    expect(defs.ble.providerFramed).toBe(shifted(source('components/providers/provider_ble/provider_ble.h'), 'PROVIDER_BLE_FRAMED'))
  })

  it('the keys are the module numbers the modules declare', () => {
    const declared = (path: string, name: string) => Number(new RegExp(String.raw`MODULE_ID_DECLARE\(${name},\s*(0x[0-9A-Fa-f]+)\)`).exec(source(path))?.[1])
    expect(defs.keys.sys).toBe(declared('components/sys/sys_system/sys_system_id.h', 'MODULE_ID_SYS'))
    expect(defs.keys.watch).toBe(declared('components/sys/sys_watch/sys_watch_id.h', 'MODULE_ID_WATCH'))
    expect(defs.control.key).toBe(declared('components/sys/sys_session/sys_session_id.h', 'MODULE_ID_SESSION'))
    expect(defs.streams.watches).toBe(defs.keys.watch)
  })

  it('the header is cmd_hdr_t: CMD_HEAD bytes, type, src, key, seq u16, and the frame limit is CMD_FRAME_MAX', () => {
    const cmd = source('components/core/cmd/cmd.h')
    expect(define(cmd, 'CMD_FRAME_MAX')).toBe(defs.frameMax)
    expect(define(cmd, 'CMD_HEAD')).toBe(defs.frameHead)
    const struct = /typedef struct __attribute__\(\(packed\)\) \{([^}]*)\} cmd_hdr_t;/.exec(cmd)![1]!
    expect([...struct.matchAll(/(uint\d+)_t\s+(\w+);/g)].map((m) => `${m[1]} ${m[2]}`)).toEqual(['uint8 type', 'uint8 src', 'uint8 key', 'uint16 seq'])
    expect(defs.watchTickMs).toBe(define(source('components/sys/sys_watch/sys_watch.h'), 'SYS_WATCH_TICK_MS'))
  })

  it('HELLO_BYTES is sys_session_hello_t', () => {
    expect(/_Static_assert\(sizeof\(sys_session_hello_t\) == (\d+)/.exec(source('components/sys/sys_session/sys_session.h'))?.[1]).toBe(String(HELLO_BYTES))
  })
})

describe('frames (CMD.MD "The header")', () => {
  it('encodes a request: [type][src][key][seq u16][dir][...]', () => {
    const i2c = defs.keys.i2c!
    expect(hex(encodeRequest(defs, 0xf0, 7, { key: i2c, body: Uint8Array.of(defs.dir.read, 0x02, 0) }))).toBe(`00 f0 ${hex(Uint8Array.of(i2c))} 07 00 00 02 00`)
    expect(encodeRequest(defs, 0xf0, 0x1234, { key: defs.keys.sys!, body: Uint8Array.of(0, 1), urgent: true }).subarray(0, 5)).toEqual(Uint8Array.of(defs.flags.urgent, 0xf0, defs.keys.sys!, 0x34, 0x12))
    expect(encodeRequest(defs, 0xf0, 7, { key: 1, body: Uint8Array.of(0), noReply: true })[0]).toBe(defs.flags.noReply)
    expect(() => encodeRequest(defs, 0xf0, 0x10000, { key: 1, body: Uint8Array.of(0) })).toThrow(RangeError)
    expect(() => encodeRequest(defs, 0xf0, 0, { key: 1, body: new Uint8Array(0) })).toThrow(RangeError)
  })

  it('decodes any frame: the kind in bits 6-7, the status of an answer in bit 2', () => {
    const answer = encodeFrame(defs, { kind: defs.kind.answer, error: true, src: 3, key: 0x26, seq: 0x107 }, Uint8Array.of(9))
    expect(answer[0]).toBe((defs.kind.answer << 6) | defs.type.status)
    expect(decodeFrame(defs, answer)).toMatchObject({ kind: defs.kind.answer, error: true, src: 3, key: 0x26, seq: 0x107 })
    expect([...decodeFrame(defs, answer)!.data]).toEqual([9])
    expect(decodeFrame(defs, answer.subarray(0, 4))).toBeUndefined()
  })

  it('a stored frame is [key][body], and back', () => {
    const request = { key: 0x26, body: Uint8Array.of(1, 4, 7) }
    expect([...requestFrame(request)]).toEqual([0x26, 1, 4, 7])
    expect(frameRequest(requestFrame(request))).toEqual(request)
  })

  it('a HELLO says the id of its sender as the source:[CONTROL, src, MODULE_ID_SESSION, 0][HELLO][boot_id u32][build_id u32]', () => {
    const mine = encodeHello(defs, { id: 0xf0, bootId: 0x12345678 })
    expect([...mine]).toEqual([defs.kind.control << 6, 0xf0, defs.control.key, 0, 0, defs.control.hello, 0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0])
    expect(decodeHello(defs, decodeFrame(defs, mine)!)).toEqual({ id: 0xf0, bootId: 0x12345678, buildId: 0 })
    const board = encodeFrame(defs, { kind: defs.kind.control, src: 3, key: defs.control.key, seq: 0 }, Uint8Array.of(defs.control.hello, 1, 2, 3, 4, 5, 6, 7, 8))
    expect(decodeHello(defs, decodeFrame(defs, board)!)).toEqual({ id: 3, bootId: 0x04030201, buildId: 0x08070605 })
    expect(decodeHello(defs, decodeFrame(defs, board.subarray(0, board.length - 1))!)).toBeUndefined() // one byte short
  })
})

describe('error records', () => {
  it('decodes an error record, naming the tags from errors.json', () => {
    const request = [defs.keys.i2c!, defs.dir.read, 0x02, 0x00]
    const data = record([['ERR_ROUTER_KEY', { key: defs.keys.i2c! }], ['ERR_I2C_BUS_STUCK', { bus: 0, addr: 0 }]], request, 7, 1_766_810_422_592)
    const parsed = decodeErrorRecord(defs, data, errorCatalog())!
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
