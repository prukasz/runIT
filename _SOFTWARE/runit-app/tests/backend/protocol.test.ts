import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import { decodeErrorRecord, decodeFrame, decodeHello, decodeResponse, encodeHello, encodeRequest, errorCatalog, fragment, Reassembler } from '@backend'
import { defs } from './fakes'

const hex = (bytes: Uint8Array) => [...bytes].map((b) => b.toString(16).padStart(2, '0')).join(' ')
const bytes = (text: string) => Uint8Array.from(text.split(' ').map((b) => Number.parseInt(b, 16)))
const source = (path: string) => readFileSync(fileURLToPath(new URL(`../../../runit-esp/${path}`, import.meta.url)), 'utf8')

describe('the numbers come from the firmware', () => {
  it('the runIT UUIDs and the fragment bits equal sys_ble.h', () => {
    const header = source('components/sys/sys_ble/core/sys_ble.h')
    const define = (name: string) => Number(new RegExp(`#define\\s+${name}\\s+(0x[0-9A-Fa-f]+)`).exec(header)?.[1])
    expect(defs.gatt).toEqual({
      service: define('SYS_BLE_SVC_RUNIT'),
      tx: define('SYS_BLE_CHR_RUNIT_TX'),
      rx: define('SYS_BLE_CHR_RUNIT_RX'),
      logs: define('SYS_BLE_CHR_RUNIT_LOGS'),
      status: define('SYS_BLE_CHT_RUNIT_STATUS'),
    })
    expect(defs.frag).toEqual({ last: define('SYS_BLE_FRAG_LAST'), first: define('SYS_BLE_FRAG_FIRST'), countMask: define('SYS_BLE_FRAG_COUNT_MASK') })
  })

  it('the root keys equal router_ids.h', () => {
    const header = source('components/core/router/router_ids.h')
    for (const m of header.matchAll(/#define\s+ROUTER_KEY_([A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+)u/g)) expect(defs.keys[m[1]!.toLowerCase()], m[1]).toBe(Number(m[2]))
    expect(defs.keys.vm).toBe(0x0b)
    expect(defs.keys.ble).toBe(0x03)
  })

  it('the session header is CMD_HEAD and the frame limit equals CMD_FRAME_MAX', () => {
    const cmd = source('components/core/cmd/cmd.h')
    expect(Number(/#define\s+CMD_FRAME_MAX\s+(\d+)u/.exec(cmd)?.[1])).toBe(defs.frameMax)
    expect(Number(/#define\s+CMD_HEAD\s+(\d+)u/.exec(cmd)?.[1])).toBe(5)
    const config = source('components/sys/sys_session/config/sys_session_config.h')
    expect(Number(/#define\s+SYS_SESSION_VERSION\s+(\d+)u/.exec(config)?.[1])).toBe(defs.session.version)
  })
})

describe('frames of the session (SYS_SESSION.MD examples)', () => {
  const to = { dst: 0x00, src: 0xf0 }
  it('encodes a request: [frame byte][dst][src][seq u16][keys][payload]', () => {
    expect(hex(encodeRequest(defs, 7, { keys: [defs.keys.i2c!, 0x02], payload: Uint8Array.of(0) }, to))).toBe('00 00 f0 07 00 02 02 00') // i2c SCAN bus 0
    expect(hex(encodeRequest(defs, 0x1234, { keys: [defs.keys.sys!, 0x01], urgent: true }, to))).toBe('01 00 f0 34 12 01 01') // sys PING, urgent
    expect(hex(encodeRequest(defs, 7, { keys: [defs.keys.sys!, 0x01], noReply: true }, to)).slice(0, 2)).toBe('02')
    expect(() => encodeRequest(defs, 0x10000, { keys: [1] }, to)).toThrow(RangeError)
    expect(() => encodeRequest(defs, 0, { keys: [] }, to)).toThrow(RangeError)
  })

  it('decodes an answer (kind 1, status in bit 2), and nothing else', () => {
    expect(decodeResponse(defs, bytes('40 f0 00 07 00 10 00 00 00'))).toMatchObject({ seq: 7, ok: true })
    expect([...decodeResponse(defs, bytes('40 f0 00 07 00 10 00 00 00'))!.data]).toEqual([0x10, 0, 0, 0])
    expect(decodeResponse(defs, bytes('44 f0 00 07 01'))).toMatchObject({ seq: 0x107, ok: false })
    expect(decodeResponse(defs, bytes('80 f0 00 07 00 02 01'))).toBeUndefined() // a stream frame (telemetry)
    expect(decodeResponse(defs, bytes('40 f0 00 07'))).toBeUndefined()
    expect(decodeFrame(defs, bytes('80 f0 00 07 00 02 01'))).toMatchObject({ kind: defs.session.kind.stream, seq: 7 })
  })

  it('a client HELLO asks for an id; the board HELLO gives it', () => {
    const mine = encodeHello(defs, 60000)
    expect(mine[0]).toBe(defs.session.control.hello)
    expect(decodeHello(defs, mine)).toMatchObject({ version: defs.session.version, boardId: defs.session.id.none, yourId: defs.session.id.none, timeoutMs: 60000 })
    const board = Uint8Array.from([defs.session.control.hello, 1, 0x00, 0xf0, 1, 2, 3, 4, 5, 6, 0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0, 0, 0, 0, 0, 0xb8, 0x0b, 0])
    expect(decodeHello(defs, board)).toEqual({ version: 1, boardId: 0, yourId: 0xf0, bootId: 0x12345678, timeoutMs: 3000 })
  })

  it('decodes the error record of the ERRORS.MD example, naming the tags from errors.json', () => {
    const frame = bytes('44 f0 00 07 00 01 40 8d 1b 5e 9b 01 00 00 f0 07 00 00 03 02 02 00 03 01 00 02 00 01 02 01 00 02 00 01 02 10 00 01 00 01 00')
    const response = decodeResponse(defs, frame)!
    const record = decodeErrorRecord(defs, response.data, errorCatalog())!
    expect(record.source).toBe('command')
    expect(record.time).toBe(0x19b5e1b8d40) // the board's clock, UTC ms
    expect(record.command).toMatchObject({ sender: 0xf0, seq: 7, flags: 0, requestCut: false })
    expect(hex(record.command!.request)).toBe('02 02 00')
    expect(record.chain.map((n) => n.info?.name)).toEqual(['ERR_ROUTER_KEY', 'ERR_ROUTER_KEY', 'ERR_I2C_REMOTE_DENIED'])
    expect(record.chain[0]!.fields).toEqual({ key: 2 })
    expect(record.cause?.info?.name).toBe('ERR_I2C_REMOTE_DENIED')
    expect(record.cause?.fields).toEqual({ bus: 0 })
    expect(record.chainCut).toBe(false)
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
    const reassembler = new Reassembler(defs, 512)
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
    const reassembler = new Reassembler(defs, 8)
    const results = fragment(defs, new Uint8Array(20), 5).map((p) => reassembler.push(p))
    expect(results.some((r) => r && 'dropped' in r)).toBe(true)
    expect(results.some((r) => r && 'frame' in r)).toBe(false)
  })
})
