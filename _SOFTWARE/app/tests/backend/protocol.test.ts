import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { CommandError, CommandTimeout, LinkClosed, RouterClient } from '../../src/backend/protocol/client'
import type { FrameLink } from '../../src/backend/protocol/client'
import { ErrorCatalog } from '../../src/backend/protocol/errors'
import { RunitBleLink } from '../../src/ble/link'
import { buildRequest, decodeErrorRecord, parseResponse } from '../../src/backend/protocol/frames'

const hex = (text: string) => Uint8Array.from(text.replace(/\s+/g, '').match(/../g)!.map((h) => parseInt(h, 16)))
const bytes = (...values: number[]) => Uint8Array.from(values)

/** The error response of ROUTER.MD's example: i2c SCAN of a bus that is not open. */
const SCAN_ERROR = hex('05 07 01  01 01 00000000 07 00  03 020200  03  01000200 00 01 02  01000200 00 01 02  10000100 02 01 00')

describe('router frames', () => {
  it('a request is [seq][flags][keys][payload]', () => {
    expect(Array.from(buildRequest(7, [0x02, 0x02], bytes(0)))).toEqual([7, 0, 2, 2, 0])
    expect(Array.from(buildRequest(7, [1, 1], undefined, true))).toEqual([7, 1, 1, 1])
    expect(() => buildRequest(1, [])).toThrow(RangeError)
    expect(() => buildRequest(1, [1], new Uint8Array(512))).toThrow(RangeError)
  })

  it('a response is [05][seq][status][data]', () => {
    expect(parseResponse(bytes(5, 7, 0, 1, 2))).toEqual({ seq: 7, status: 0, data: bytes(1, 2) })
    expect(parseResponse(bytes(2, 7, 0))).toBeUndefined()
    expect(parseResponse(bytes(5, 7))).toBeUndefined()
  })

  it('decodes the error record of the router docs', () => {
    const response = parseResponse(SCAN_ERROR)!
    expect(response.status).toBe(1)
    const record = decodeErrorRecord(response.data)
    expect(record).toMatchObject({ source: 'command', link: 1, peer: 0, seq: 7, flags: 0, requestCut: false, chainCut: false })
    expect(Array.from(record.request!)).toEqual([2, 2, 0])
    expect(record.nodes.map((n) => [n.module, n.code, n.severity])).toEqual([[2, 1, 0], [2, 1, 0], [1, 16, 2]])
  })

  it('an internal record has no command part, and a cut one is refused', () => {
    expect(decodeErrorRecord(hex('00 81 01000200 03 01 05'))).toMatchObject({ source: 'internal', chainCut: true, nodes: [{ tag: 0x00020001, severity: 3, payload: bytes(5) }] })
    expect(() => decodeErrorRecord(hex('01 01 00'))).toThrow(RangeError)
    expect(() => decodeErrorRecord(hex('09 00'))).toThrow(RangeError)
    expect(() => decodeErrorRecord(hex('00 01 01000200 03 04 05'))).toThrow(RangeError)
  })
})

describe('ErrorCatalog (data-structures/errors/errors.generated.json)', () => {
  const catalog = new ErrorCatalog()

  it('names the tags and decodes the payload by the generated layout', () => {
    const record = decodeErrorRecord(parseResponse(SCAN_ERROR)!.data)
    const chain = catalog.chain(record)
    expect(chain.map((n) => n.name)).toEqual(['ERR_ROUTER_KEY', 'ERR_ROUTER_KEY', 'ERR_I2C_REMOTE_DENIED'])
    expect(chain[0].fields).toEqual({ key: 2 })
    expect(chain[2].fields).toEqual({ bus: 0 })
    expect(catalog.rootCause(record)).toBe('ERR_I2C_REMOTE_DENIED')
    expect(catalog.format(record)).toBe('ERR_I2C_REMOTE_DENIED (bus=0)')
  })

  it('a payload field of an enum is the member name, signed fields are signed, text is text', () => {
    const range = catalog.describe({ tag: 0x00060010, module: 6, code: 16, severity: 3, payload: hex('2a00 f6ffffff 00000000 64000000') })
    expect(range.name).toBe('ERR_DEV_RANGE')
    expect(range.fields).toEqual({ id: 42, value: -10, min: 0, max: 100 })
    const type = catalog.describe({ tag: 0x000f0012, module: 15, code: 18, severity: 1, payload: bytes(5) })
    expect(type.name).toBe('ERR_VM_OBJ_BAD_TYPE')
    expect(typeof type.fields.type).toBe('string') // vm_obj_t_e member
    const name = new Uint8Array(19)
    name.set([7, 0, 3, ...new TextEncoder().encode('speed')])
    expect(catalog.describe({ tag: 0x000f0008, module: 15, code: 8, severity: 1, payload: name }).fields).toMatchObject({ id: 7, chain_pos: 3, name: 'speed' })
  })

  it('an unknown tag (a newer firmware) is still shown, not dropped', () => {
    const node = catalog.describe({ tag: 0x7fff0001, module: 0x7fff, code: 1, severity: 2, payload: bytes() })
    expect(node).toMatchObject({ unknown: true, name: '0x7fff0001' })
  })

  it('a payload shorter than the layout (an older firmware) decodes what is there', () => {
    expect(catalog.describe({ tag: 0x00060010, module: 6, code: 16, severity: 3, payload: hex('2a00') }).fields).toEqual({ id: 42 })
  })
})

/** A link that records requests and lets a test answer them. */
const fakeLink = () => {
  const handlers = new Set<(stream: 'main' | 'logs', frame: Uint8Array) => void>()
  const sent: Uint8Array[] = []
  const link: FrameLink = {
    send: async (frame) => void sent.push(frame),
    onFrame: (handler) => (handlers.add(handler), () => handlers.delete(handler)),
  }
  const deliver = (frame: Uint8Array) => handlers.forEach((h) => h('main', frame))
  return { link, sent, deliver }
}

it('the BLE link is a FrameLink', () => {
  const link: FrameLink = new RunitBleLink({ storage: undefined })
  expect(typeof link.send).toBe('function')
})

describe('RouterClient', () => {
  beforeEach(() => vi.useFakeTimers())
  afterEach(() => vi.useRealTimers())

  it('matches answers to requests by seq, also out of order', async () => {
    const { link, sent, deliver } = fakeLink()
    const client = new RouterClient(link)
    const first = client.request([1, 1])
    const second = client.request([1, 5])
    await vi.advanceTimersByTimeAsync(0)
    expect(sent.map((f) => f[0])).toEqual([1, 2])
    deliver(bytes(5, 2, 0, 0xbb))
    deliver(bytes(5, 1, 0, 0xaa))
    expect(await second).toEqual(bytes(0xbb))
    expect(await first).toEqual(bytes(0xaa))
  })

  it('an error answer rejects with the board\'s root cause named', async () => {
    const { link, sent, deliver } = fakeLink()
    const client = new RouterClient(link)
    const pending = client.request([2, 2], bytes(0))
    const failed = pending.catch((e: unknown) => e)
    await vi.advanceTimersByTimeAsync(0)
    deliver(Uint8Array.from([...SCAN_ERROR.subarray(0, 1), sent[0][0], ...SCAN_ERROR.subarray(2)]))
    const error = (await failed) as CommandError
    expect(error).toBeInstanceOf(CommandError)
    expect(error.cause_).toBe('ERR_I2C_REMOTE_DENIED')
    expect(error.message).toContain('bus=0')
    expect(error.record?.seq).toBe(7)
  })

  it('a request with no answer times out and the late answer is ignored', async () => {
    const { link, deliver } = fakeLink()
    const client = new RouterClient(link, { timeoutMs: 500 })
    const failed = client.request([1, 1]).catch((e: unknown) => e)
    await vi.advanceTimersByTimeAsync(600)
    expect(await failed).toBeInstanceOf(CommandTimeout)
    expect(() => deliver(bytes(5, 1, 0))).not.toThrow()
  })

  it('a command with several answers collects them all (actions READ)', async () => {
    const { link, deliver } = fakeLink()
    const client = new RouterClient(link)
    const all = client.requestMany([6, 7], undefined, 3)
    await vi.advanceTimersByTimeAsync(0)
    deliver(bytes(5, 1, 0, 1))
    deliver(bytes(5, 1, 0, 2))
    deliver(bytes(5, 1, 0, 3))
    expect(await all).toEqual([bytes(1), bytes(2), bytes(3)])
  })

  it('routes the streams: telemetry, log lines, errors, events', async () => {
    const { link, deliver } = fakeLink()
    const client = new RouterClient(link)
    const seen: string[] = []
    client.onTelemetry((f) => seen.push(`tele ${f.length}`))
    client.onLog((line) => seen.push(`log ${line}`))
    client.onError((r) => seen.push(`err ${client.catalog.rootCause(r)}`))
    client.onEvent((f) => seen.push(`event ${f[0]}`))
    deliver(bytes(2, 0x0b, 4, 0))
    deliver(Uint8Array.from([3, ...new TextEncoder().encode('I (12) boot: ready\n')]))
    deliver(Uint8Array.from([4, ...SCAN_ERROR.subarray(3)]))
    deliver(bytes(6, 1, 9))
    deliver(bytes(4, 9)) // a cut error record is dropped, not thrown
    expect(seen).toEqual(['tele 3', 'log I (12) boot: ready', 'err ERR_I2C_REMOTE_DENIED', 'event 1'])
  })

  it('close() rejects what waits and stops listening', async () => {
    const { link, deliver } = fakeLink()
    const client = new RouterClient(link)
    const waiting = client.request([1, 1]).catch((e: unknown) => e)
    await vi.advanceTimersByTimeAsync(0)
    client.close('gone')
    expect(await waiting).toBeInstanceOf(LinkClosed)
    const seen: number[] = []
    client.onTelemetry(() => seen.push(1))
    deliver(bytes(2, 0))
    expect(seen).toEqual([])
  })

  it('a link that cannot send rejects the request at once', async () => {
    const link: FrameLink = { send: async () => { throw new Error('not connected') }, onFrame: () => () => undefined }
    await expect(new RouterClient(link).request([1, 1])).rejects.toThrow('not connected')
  })

  it('seq numbers stay in 1..199 and skip the ones in use', async () => {
    const { link, sent } = fakeLink()
    const client = new RouterClient(link, { timeoutMs: 10 })
    const all = Array.from({ length: 199 }, () => client.request([1, 1]).catch(() => undefined))
    await vi.advanceTimersByTimeAsync(0)
    expect(new Set(sent.map((f) => f[0])).size).toBe(199)
    expect(Math.max(...sent.map((f) => f[0]))).toBe(199)
    await expect(client.request([1, 1])).rejects.toThrow('Too many')
    await vi.advanceTimersByTimeAsync(50)
    await Promise.all(all)
  })
})
