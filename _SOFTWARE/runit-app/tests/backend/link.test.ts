import { describe, expect, it } from 'vitest'
import { CommandError, RunitLink } from '@backend'
import type { LinkStatus } from '@backend'
import { answer, defs, FakeBoard, until } from './fakes'

const PING = { keys: [defs.keys.sys!, 0x01] }

/** A board that answers every request with the payload it got after the keys (an echo). */
const echo = (board: FakeBoard, keys = 2) => {
  board.respond = (frame, send) => send(answer(frame[0]!, [...frame.subarray(2 + keys)]))
}

const linkTo = async (board: FakeBoard, options: ConstructorParameters<typeof RunitLink>[1] = {}) => {
  const link = new RunitLink(board, { timeoutMs: 100, reconnect: { delaysMs: [0, 1, 2], openTimeoutMs: 50 }, ...options })
  await link.connect(board.device)
  return link
}

describe('commands', () => {
  it('sends a request and returns the answer; the largest request is one write', async () => {
    const board = new FakeBoard()
    echo(board)
    const link = await linkTo(board)
    expect([...(await link.request({ ...PING, payload: Uint8Array.of(1, 2, 3) }))]).toEqual([1, 2, 3])
    const big = Uint8Array.from({ length: 480 }, (_, i) => i & 0xff)
    expect(await link.request({ ...PING, payload: big })).toEqual(big)
    expect(board.packets.slice(1)).toEqual([485]) // a 484 B frame: one packet, the header on it
    const max = new Uint8Array(defs.frameMax - defs.frameHead - 2).fill(7) // keys and payload: CMD_DATA_MAX
    expect(await link.request({ ...PING, payload: max })).toEqual(max)
    expect(board.packets.slice(2)).toEqual([2 + max.length + 3]) // [seq][flags], the two keys and the fragment byte
    await link.close()
  })

  it('matches answers by seq when they come out of order, and several can be on the wire at once', async () => {
    const board = new FakeBoard()
    const held: number[] = []
    board.respond = (frame, send) => {
      held.push(frame[0]!)
      if (held.length === 3) for (const seq of [...held].reverse()) send(answer(seq, [seq]))
    }
    const link = await linkTo(board)
    const results = await Promise.all([PING, PING, PING].map((r) => link.request(r)))
    expect(results.map((r) => r[0])).toEqual(held) // each got its own answer
    await link.close()
  })

  it('an ERROR answer rejects with the decoded record', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!, [0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x01, 0x05], false)) // internal, clock 0; ERR_ROUTER_KEY key 5
    const link = await linkTo(board)
    const error = await link.request(PING).catch((e: unknown) => e)
    expect(error).toBeInstanceOf(CommandError)
    expect((error as CommandError).code).toBe('device-error')
    expect((error as CommandError).record?.cause?.info?.name).toBe('ERR_ROUTER_KEY')
    expect((error as CommandError).message).toMatch(/ERR_ROUTER_KEY/)
    await link.close()
  })

  it('times out when nothing answers, and a late answer is only observed', async () => {
    const board = new FakeBoard()
    let seq = 0
    board.respond = (frame) => void (seq = frame[0]!)
    const link = await linkTo(board, { timeoutMs: 20 })
    const events: string[] = []
    link.commands.observe((e) => events.push(e.type))
    await expect(link.request(PING)).rejects.toMatchObject({ code: 'timeout' })
    board.notify(answer(seq, [9]))
    await until(() => events.includes('unmatched'))
    expect(events).toEqual(['sent', 'timeout', 'unmatched'])
    await link.close()
  })

  it('refuses a request over the board frame limit without sending it', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    await expect(link.request({ ...PING, payload: new Uint8Array(defs.frameMax - defs.frameHead - 1) })).rejects.toMatchObject({ code: 'too-long' })
    expect(board.requests).toHaveLength(0)
    await link.close()
  })

  it('frames of two requests never interleave on the wire', async () => {
    const board = new FakeBoard()
    board.writeDelayMs = 1
    echo(board)
    const link = await linkTo(board)
    const a = new Uint8Array(400).fill(0xaa)
    const b = new Uint8Array(400).fill(0xbb)
    const [ra, rb] = await Promise.all([link.request({ ...PING, payload: a }), link.request({ ...PING, payload: b })])
    expect(ra).toEqual(a)
    expect(rb).toEqual(b)
    await link.close()
  })

  it('an urgent request goes before the queued ones and past a full window', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board, { maxInFlight: 2, timeoutMs: 1000 })
    const normal = [1, 2, 3, 4].map((n) => link.request({ ...PING, payload: Uint8Array.of(n) }).catch(() => undefined)) // never answered
    await until(() => board.requests.length === 2) // the window is full, two wait
    void link.request({ ...PING, urgent: true }).catch(() => undefined)
    await until(() => board.requests.length === 3)
    expect(board.requests[2]![1]).toBe(defs.flags.urgent)
    await link.close()
    await Promise.all(normal)
  })

  it('urgent requests set the flag the firmware published', async () => {
    const board = new FakeBoard()
    echo(board)
    const link = await linkTo(board)
    await link.request({ ...PING, urgent: true })
    expect(board.requests[0]![1]).toBe(defs.flags.urgent)
    await link.close()
  })
})

describe('streams', () => {
  it('routes frames that are not answers by their stream byte, without it', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    const seen: [string | undefined, number[]][] = []
    link.onStream((f) => seen.push([f.name, [...f.data]]))
    board.notify(Uint8Array.of(defs.streams.telemetry, 1, 2, 3))
    board.notify(Uint8Array.of(defs.streams.logs, 9), defs.gatt.logs)
    board.notify(Uint8Array.of(defs.streams.errors, 7))
    board.notify(Uint8Array.of(defs.streams.events, 5))
    await until(() => seen.length === 4)
    expect(seen).toEqual([['telemetry', [1, 2, 3]], ['logs', [9]], ['errors', [7]], ['events', [5]]])
    await link.close()
  })

  it('reassembles each characteristic on its own', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    const seen: number[] = []
    link.onStream((f) => seen.push(f.data.length))
    board.notify(new Uint8Array(100).fill(defs.streams.telemetry), defs.gatt.tx)
    board.notify(new Uint8Array(60).fill(defs.streams.logs), defs.gatt.logs)
    await until(() => seen.length === 2)
    expect(seen.sort((a, b) => a - b)).toEqual([59, 99])
    await link.close()
  })

  it('reports a broken frame instead of delivering it', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    const diagnostics: string[] = []
    link.onDiagnostic((d) => diagnostics.push(d.kind))
    link.onStream(() => diagnostics.push('frame'))
    board.notify(Uint8Array.of(defs.streams.logs, 1), defs.gatt.logs) // a good frame...
    await until(() => diagnostics.length === 1)
    ;(link as unknown as { onPacket(c: number, p: Uint8Array): void }).onPacket(defs.gatt.logs, Uint8Array.of(0x05, 1, 2)) // ...then a packet outside a frame
    expect(diagnostics).toEqual(['frame', 'dropped'])
    await link.close()
  })
})

describe('a link that keeps itself up', () => {
  const statuses = (link: RunitLink) => {
    const seen: LinkStatus[] = []
    link.onStatus((s) => seen.push(s))
    return seen
  }

  it('reopens at once when the board drops, and fails what was on the wire with link-lost', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    const seen = statuses(link)
    const waiting = link.request(PING).catch((e: unknown) => e) // nobody answers
    await until(() => board.requests.length === 1)
    board.drop()
    expect(((await waiting) as CommandError).code).toBe('link-lost')
    await until(() => link.state === 'connected')
    expect(board.opens).toBe(2)
    expect(seen.map((s) => s.state)).toEqual(['reconnecting', 'connected'])
    expect(seen[0]).toMatchObject({ reason: 'out of range', attempt: 0 })
    echo(board)
    expect((await link.request(PING)).length).toBe(0) // and it works again
    await link.close()
  })

  it('a command given while the link is coming back waits for it', async () => {
    const board = new FakeBoard()
    echo(board)
    const link = await linkTo(board)
    board.failOpens = 2
    board.drop()
    const result = await link.request({ ...PING, payload: Uint8Array.of(42) })
    expect([...result]).toEqual([42])
    expect(board.opens).toBe(4) // the first, two that failed, the one that worked
    await link.close()
  })

  it('keeps trying with growing delays until the board is back', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    const seen = statuses(link)
    board.failOpens = 5
    board.drop()
    await until(() => link.state === 'connected', 1000)
    expect(seen.filter((s) => s.state === 'reconnecting').map((s) => s.attempt)).toContain(5)
    expect(board.opens).toBe(7)
    await link.close()
  })

  it('disconnect() stops the reconnecting, and a command then fails not-connected', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    board.failOpens = 1000
    board.drop()
    await until(() => board.opens > 3)
    await link.disconnect()
    const opens = board.opens
    await new Promise((r) => setTimeout(r, 30))
    expect(board.opens).toBe(opens)
    expect(link.state).toBe('idle')
    await expect(link.request(PING)).rejects.toMatchObject({ code: 'not-connected' })
  })

  it('gives up after maxAttempts', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board, { reconnect: { delaysMs: [0], maxAttempts: 3 } })
    board.failOpens = 1000
    board.drop()
    await until(() => link.state === 'idle')
    expect(board.opens).toBe(1 + 3)
  })

  it('without reconnect a lost link stays lost', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board, { reconnect: false })
    board.drop('gone')
    expect(link.state).toBe('idle')
    expect(board.opens).toBe(1)
  })

  it('recycles a link that stops answering though nothing said it is gone', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board, { timeoutMs: 15, stallAfter: 2 })
    const results = await Promise.allSettled([link.request(PING), link.request(PING)])
    expect(results.every((r) => r.status === 'rejected')).toBe(true)
    await until(() => board.opens === 2 && link.state === 'connected')
    expect(board.closes).toBe(1) // the dead channel was closed
    await link.close()
  })

  it('an open that hangs is dropped after the limit and retried; one that finishes late is closed', async () => {
    const board = new FakeBoard()
    const link = new RunitLink(board, { reconnect: { delaysMs: [0], openTimeoutMs: 20 } })
    board.hangOpens = 1
    await link.connect(board.device, { retry: true })
    expect(link.state).toBe('connected')
    expect(board.opens).toBe(2)
    board.release() // the first open finishes now: nobody wants it
    await until(() => board.closes === 1)
    expect(link.state).toBe('connected')
    await link.close()
  })

  it('reopen() drops the link and opens it again (after the board changed its GATT table)', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    link.reopen()
    await until(() => board.opens === 2 && link.state === 'connected')
    expect(board.closes).toBe(1)
    await link.close()
  })

  it('a failed first connect throws and leaves the link idle; with retry it keeps going', async () => {
    const board = new FakeBoard()
    board.failOpens = 1
    const link = new RunitLink(board, { reconnect: { delaysMs: [0, 1] } })
    await expect(link.connect(board.device)).rejects.toThrow(/GATT connect failed/)
    expect(link.state).toBe('idle')
    board.failOpens = 2
    await link.connect(board.device, { retry: true })
    expect(link.state).toBe('connected')
    await link.close()
  })

  it('a write that fails recycles the link at once (the OS reports no disconnect after a GATT change)', async () => {
    const board = new FakeBoard()
    echo(board)
    const link = await linkTo(board)
    board.failNextWrite = new Error('The object has been closed')
    await expect(link.request(PING)).rejects.toMatchObject({ code: 'send-failed' })
    await until(() => board.opens === 2 && link.state === 'connected')
    expect(board.closes).toBe(1)
    expect((await link.request(PING)).length).toBe(0)
    await link.close()
  })

  it('a packet is never longer than the 512 B ATT value', async () => {
    const board = new FakeBoard()
    echo(board)
    const link = await linkTo(board, { packetBytes: 4000 })
    await link.request({ ...PING, payload: new Uint8Array(500) })
    expect(Math.max(...board.packets)).toBeLessThanOrEqual(512)
    await link.close()
  })

  it('a write that fails fails its command with send-failed, and the link carries on', async () => {
    const board = new FakeBoard()
    echo(board)
    const link = await linkTo(board)
    board.failNextWrite = new Error('GATT error')
    await expect(link.request(PING)).rejects.toMatchObject({ code: 'send-failed' })
    expect((await link.request(PING)).length).toBe(0)
    expect(link.state).toBe('connected')
    await link.close()
  })
})
