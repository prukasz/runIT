import { afterEach, describe, expect, it, vi } from 'vitest'
import type { OutgoingMessage, ReceivedFrame } from '../stream'
import { ReceivedDataStream } from '../stream'
import { CommandClient, CommandError } from './CommandClient'
import type { CommandEvent } from './CommandClient'
import { decodeInterfaceResponse, encodeCommandFrame } from './frames'

const TARGET = 'board-1'
// Test values on purpose different from the firmware's, so nothing here depends on them.
const PROTOCOL = { responseStream: 0x15, statusOk: 3 }
const OK = PROTOCOL.statusOk
const FAIL = 7

/** Records every write; answers are pushed by the test through the received stream. */
class Harness {
  readonly sent: OutgoingMessage[] = []
  readonly received = new ReceivedDataStream()
  readonly events: CommandEvent[] = []
  sendImpl: (message: OutgoingMessage) => Promise<void> = async () => undefined
  readonly client: CommandClient

  constructor(options: { maxInFlight?: number; timeoutMs?: number } = {}) {
    this.client = new CommandClient({
      sender: { send: (message) => { this.sent.push(message); return this.sendImpl(message) } },
      frames: this.received,
      protocol: PROTOCOL,
      targetId: TARGET,
      ...options,
    })
    this.client.observe((event) => this.events.push(event))
  }

  /** Answer as the board: [stream][seq][class][packet][status][data]. */
  answer(seq: number, cls: number, packet: number, status = OK, data: number[] = [], targetId = TARGET): void {
    const frame: ReceivedFrame = { data: Uint8Array.from([PROTOCOL.responseStream, seq, cls, packet, status, ...data]), transport: 'test', targetId, endpoint: 'tx', receivedAt: Date.now() }
    this.received.publish(frame)
  }

  seqOf(index: number): number {
    return this.sent[index].data[0]
  }
}

const flush = () => new Promise<void>((resolve) => setTimeout(resolve, 0))
const body = (...bytes: number[]) => Uint8Array.from(bytes)

afterEach(() => {
  vi.useRealTimers()
})

describe('frames', () => {
  it('prefixes the sequence byte and decodes answers', () => {
    expect([...encodeCommandFrame(7, body(0x01, 0x24, 3, 4))]).toEqual([7, 0x01, 0x24, 3, 4])
    expect(() => encodeCommandFrame(256, body(1))).toThrow(RangeError)
    const decoded = decodeInterfaceResponse(Uint8Array.from([PROTOCOL.responseStream, 7, 0x01, 0x21, FAIL, 0x02, 0xa0, 0x10, 0x00]), PROTOCOL)
    expect(decoded).toMatchObject({ seq: 7, requestClass: 0x01, requestPacket: 0x21, status: FAIL, error: { tag: 0xa002, owner: 0x0010 } })
    expect(decodeInterfaceResponse(Uint8Array.from([PROTOCOL.responseStream, 7, 1, 1, OK, 1, 2, 3, 4]), PROTOCOL)?.error).toBeUndefined()
    expect(decodeInterfaceResponse(Uint8Array.from([0x02, 7, 1, 1, OK]), PROTOCOL)).toBeUndefined()
    expect(decodeInterfaceResponse(Uint8Array.from([PROTOCOL.responseStream, 7, 1, 1]), PROTOCOL)).toBeUndefined()
  })
})

describe('CommandClient', () => {
  it('sends [seq][body] on the interface route and resolves with the matching answer', async () => {
    const h = new Harness()
    const pending = h.client.send({ body: body(0x01, 0x21, 2, 5), label: 'get_level' })
    await flush()
    expect(h.sent).toHaveLength(1)
    expect(h.sent[0]).toMatchObject({ targetId: TARGET, route: 'runit.interface' })
    expect([...h.sent[0].data.slice(1)]).toEqual([0x01, 0x21, 2, 5])
    h.answer(h.seqOf(0), 0x01, 0x21, OK, [2, 5, 1])
    const response = await pending
    expect(response.ok).toBe(true)
    expect([...response.data]).toEqual([2, 5, 1])
    expect(response.label).toBe('get_level')
    expect(h.events.map((event) => event.type)).toEqual(['sent', 'response'])
  })

  it('resolves an ERROR answer from send() and rejects it from call()', async () => {
    const h = new Harness()
    const sent = h.client.send({ body: body(0x01, 0x21, 77, 1) })
    await flush()
    h.answer(h.seqOf(0), 0x01, 0x21, FAIL, [0x02, 0xa0, 0x10, 0x00])
    expect(await sent).toMatchObject({ ok: false, error: { tag: 0xa002, owner: 0x0010 } })

    const called = h.client.call({ body: body(0x01, 0x21, 77, 1) })
    await flush()
    h.answer(h.seqOf(1), 0x01, 0x21, FAIL, [0x02, 0xa0, 0x10, 0x00])
    await expect(called).rejects.toMatchObject({ code: 'device-error' })
  })

  it('ignores other boards, other streams and stale answers', async () => {
    const h = new Harness()
    const pending = h.client.send({ body: body(0x01, 0x24, 3, 4) })
    await flush()
    const seq = h.seqOf(0)
    h.answer(seq, 0x01, 0x24, OK, [], 'other-board')
    h.received.publish({ data: Uint8Array.from([0x02, seq, 0x01, 0x24, OK]), transport: 'test', targetId: TARGET, endpoint: 'tx', receivedAt: 0 })
    h.answer(seq, 0x01, 0x99) // same seq, different packet: an earlier command's late answer
    expect(h.events.filter((event) => event.type === 'unmatched')).toHaveLength(1)
    h.answer(seq, 0x01, 0x24)
    await expect(pending).resolves.toMatchObject({ ok: true, seq })
  })

  it('times out, then reports the late answer as unmatched', async () => {
    vi.useFakeTimers()
    const h = new Harness({ timeoutMs: 100 })
    const pending = h.client.send({ body: body(0x01, 0x24, 3, 4) })
    const assertion = expect(pending).rejects.toMatchObject({ code: 'timeout' })
    await vi.advanceTimersByTimeAsync(101)
    await assertion
    h.answer(h.seqOf(0), 0x01, 0x24)
    expect(h.events.map((event) => event.type)).toEqual(['sent', 'timeout', 'unmatched'])
  })

  it('keeps one command in flight by default and sends the next after the answer', async () => {
    const h = new Harness()
    const first = h.client.send({ body: body(0x01, 0x24, 0, 1) })
    const second = h.client.send({ body: body(0x01, 0x24, 0, 2) })
    await flush()
    expect(h.sent).toHaveLength(1)
    expect(h.client.pendingCount).toBe(2)
    h.answer(h.seqOf(0), 0x01, 0x24)
    await first
    await flush()
    expect(h.sent).toHaveLength(2)
    expect(h.seqOf(1)).toBe((h.seqOf(0) + 1) & 0xff)
    h.answer(h.seqOf(1), 0x01, 0x24)
    await second
  })

  it('pipelines up to maxInFlight and wraps the sequence byte past in-flight ones', async () => {
    const h = new Harness({ maxInFlight: 3 })
    const all = Array.from({ length: 258 }, (_, index) => h.client.send({ body: body(0x01, 0x24, 0, index & 0xff) }))
    await flush()
    expect(h.sent).toHaveLength(3)
    for (let index = 0; index < 258; index += 1) {
      await flush()
      h.answer(h.seqOf(index), 0x01, 0x24)
    }
    await Promise.all(all)
    const seqs = h.sent.map((message) => message.data[0])
    expect(seqs.slice(0, 3)).toEqual([0, 1, 2])
    expect(seqs[256]).toBe(0)
  })

  it('rejects a failed write and carries on with the queue', async () => {
    const h = new Harness()
    h.sendImpl = async () => { throw new Error('GATT write failed') }
    const failed = h.client.send({ body: body(0x01, 0x24, 0, 1) })
    await expect(failed).rejects.toMatchObject({ code: 'send-failed' })
    h.sendImpl = async () => undefined
    const next = h.client.send({ body: body(0x01, 0x24, 0, 2) })
    await flush()
    h.answer(h.seqOf(1), 0x01, 0x24)
    await expect(next).resolves.toMatchObject({ ok: true })
  })

  it('close() cancels everything queued or waiting', async () => {
    const h = new Harness()
    const first = h.client.send({ body: body(0x01, 0x24, 0, 1) })
    const second = h.client.send({ body: body(0x01, 0x24, 0, 2) })
    await flush()
    h.client.close('disconnected')
    await expect(first).rejects.toBeInstanceOf(CommandError)
    await expect(second).rejects.toMatchObject({ code: 'cancelled', message: 'disconnected' })
    await expect(h.client.send({ body: body(1) })).rejects.toMatchObject({ code: 'cancelled' })
  })
})
