import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import { decodeErrorRecord, decodeResponse, encodeRequest, errorCatalog, fragment, Reassembler } from '@backend'
import { defs } from './fakes'

const hex = (bytes: Uint8Array) => [...bytes].map((b) => b.toString(16).padStart(2, '0')).join(' ')
const bytes = (text: string) => Uint8Array.from(text.split(' ').map((b) => Number.parseInt(b, 16)))
const source = (path: string) => readFileSync(fileURLToPath(new URL(`../../../${path}`, import.meta.url)), 'utf8')

describe('the numbers come from the firmware', () => {
  it('the runIT UUIDs and the fragment bits equal sys_ble.h', () => {
    const header = source('components/sys/sys_ble/sys_ble.h')
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

  it('the response stream equals CONFIG_ROUTER_RESPONSE_STREAM and the frame limit equals CONFIG_ROUTER_FRAME_MAX', () => {
    const kconfig = source('components/core/router/Kconfig')
    expect(Number(/config ROUTER_RESPONSE_STREAM[\s\S]*?default (0x[0-9a-fA-F]+)/.exec(kconfig)?.[1])).toBe(defs.streams.response)
    expect(Number(/config ROUTER_FRAME_MAX[\s\S]*?default (\d+)/.exec(kconfig)?.[1])).toBe(defs.frameMax)
  })
})

describe('requests and responses (ROUTER.MD examples)', () => {
  it('encodes [seq][flags][keys][payload]', () => {
    expect(hex(encodeRequest(defs, 7, { keys: [defs.keys.i2c!, 0x02], payload: Uint8Array.of(0) }))).toBe('07 00 02 02 00') // i2c SCAN bus 0
    expect(hex(encodeRequest(defs, 7, { keys: [defs.keys.sys!, 0x01], urgent: true }))).toBe('07 01 01 01') // sys PING, urgent
    expect(() => encodeRequest(defs, 256, { keys: [1] })).toThrow(RangeError)
    expect(() => encodeRequest(defs, 0, { keys: [] })).toThrow(RangeError)
  })

  it('decodes an answer of the response stream, and ignores any other stream', () => {
    expect(decodeResponse(defs, bytes('05 07 00 10 00 00 00'))).toMatchObject({ seq: 7, ok: true })
    expect([...decodeResponse(defs, bytes('05 07 00 10 00 00 00'))!.data]).toEqual([0x10, 0, 0, 0])
    expect(decodeResponse(defs, bytes('05 07 01'))).toMatchObject({ ok: false })
    expect(decodeResponse(defs, bytes('02 07 00 01'))).toBeUndefined() // telemetry
    expect(decodeResponse(defs, bytes('05 07'))).toBeUndefined()
  })

  it('decodes the error record of the ROUTER.MD example, naming the tags from errors.json', () => {
    const frame = bytes('05 07 01 01 01 00 00 00 00 07 00 03 02 02 00 03 01 00 02 00 00 01 02 01 00 02 00 00 01 02 10 00 01 00 02 01 00')
    const response = decodeResponse(defs, frame)!
    const record = decodeErrorRecord(defs, response.data, errorCatalog())!
    expect(record.source).toBe('command')
    expect(record.command).toMatchObject({ link: 1, peer: 0, seq: 7, flags: 0, requestCut: false })
    expect(hex(record.command!.request)).toBe('02 02 00')
    expect(record.chain.map((n) => n.info?.name)).toEqual(['ERR_ROUTER_KEY', 'ERR_ROUTER_KEY', 'ERR_I2C_REMOTE_DENIED'])
    expect(record.chain[0]!.fields).toEqual({ key: 2 })
    expect(record.cause?.info?.name).toBe('ERR_I2C_REMOTE_DENIED')
    expect(record.cause?.fields).toEqual({ bus: 0 })
    expect(record.chainCut).toBe(false)
  })

  it('keeps the raw payload of a tag it does not know, and refuses a record that is cut short', () => {
    const data = bytes('00 01 78 56 34 12 02 01 aa')
    const record = decodeErrorRecord(defs, data, errorCatalog())!
    expect(record.source).toBe('internal')
    expect(record.cause).toMatchObject({ tag: 0x12345678, severity: 2 })
    expect(record.cause?.info).toBeUndefined()
    expect(decodeErrorRecord(defs, data.subarray(0, 8), errorCatalog())).toBeUndefined()
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
