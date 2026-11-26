import { describe, expect, it } from 'vitest'
import { CommandError, errorCatalog, RunitLink } from '@backend'
import type { LinkStatus, Request } from '@backend'
import { answer, CLIENT_ID, defs, FakeBoard, until } from './fakes'

/** A request of the SYS router: its read of op 1 with `args`. */
const ping = (args: number[] | Uint8Array = [], extra: Partial<Request> = {}): Request => ({ key: defs.keys.sys!, body: Uint8Array.from([defs.dir.read, 0x01, ...args]), ...extra })

/** A board that answers every request with the arguments it got after `[dir][op]` (an echo). */
const echo = (board: FakeBoard) => {
  board.respond = (r, send) => send(answer(r, r.data.subarray(2)))
}

const linkTo = async (board: FakeBoard, options: ConstructorParameters<typeof RunitLink>[1] = {}) => {
  const link = new RunitLink(board, { timeoutMs: 100, reconnect: { delaysMs: [0, 1, 2], openTimeoutMs: 50 }, ...options })
  await link.connect(board.device)
  return link
}

describe('opening', () => {
  it('opens the runIT service: written on the session characteristic, heard on it and on the raw logs and telemetry', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    expect(board.spec).toEqual({ service: defs.gatt.service, write: defs.gatt.session, notify: [defs.gatt.session], optional: [defs.gatt.logs, defs.gatt.telemetry] })
    expect(board.clientHello?.id).toBe(CLIENT_ID)
    expect(link.session).toMatchObject({ clientId: CLIENT_ID, boardId: board.boardId, bootId: board.bootId, buildId: 0xb17d1d, deadmanMs: 0 })
    await link.close()
  })

  it('a client id outside the clients\' range is refused', () => {
    expect(() => new RunitLink(new FakeBoard(), { clientId: defs.src.boardLast })).toThrow(RangeError)
  })
})

describe('requests', () => {
  it('sends a request with this client as its source and returns the answer; the largest request is two writes', async () => {
    const board = new FakeBoard()
    echo(board)
    const link = await linkTo(board)
    expect([...(await link.request(ping([1, 2, 3])))]).toEqual([1, 2, 3])
    expect(board.requests[0]).toMatchObject({ kind: defs.kind.request, src: CLIENT_ID, key: defs.keys.sys })
    const big = Uint8Array.from({ length: 480 }, (_, i) => i & 0xff)
    expect(await link.request(ping(big))).toEqual(big)
    expect(board.packets.slice(1)).toEqual([defs.frameHead + 2 + 480 + 1]) // one packet, its fragment byte on it
    const max = new Uint8Array(defs.frameMax - defs.frameHead - 2).fill(7) // a body of CMD_DATA_MAX
    expect(await link.request(ping(max))).toEqual(max)
    expect(board.packets.slice(2)).toEqual([512, 2]) // with the fragment byte one over the 512 B a write carries
    await link.close()
  })

  it('matches answers by seq when they come out of order, and several can be on the wire at once', async () => {
    const board = new FakeBoard()
    const held: (typeof board.requests)[number][] = []
    board.respond = (r, send) => {
      held.push(r)
      if (held.length === 3) for (const h of [...held].reverse()) send(answer(h, [h.seq & 0xff]))
    }
    const link = await linkTo(board)
    const results = await Promise.all([ping(), ping(), ping()].map((r) => link.request(r)))
    expect(results.map((r) => r[0])).toEqual(held.map((h) => h.seq & 0xff))
    await link.close()
  })

  it('an answer with another key than its request\'s settles nothing', async () => {
    const board = new FakeBoard()
    board.respond = (r, send) => send(answer({ key: r.key + 1, seq: r.seq }, [1]))
    const link = await linkTo(board, { timeoutMs: 20 })
    const events: string[] = []
    link.commands.observe((e) => events.push(e.type))
    await expect(link.request(ping())).rejects.toMatchObject({ code: 'timeout' })
    expect(events).toContain('unmatched')
    await link.close()
  })

  it('an ERROR answer rejects with the decoded record', async () => {
    const board = new FakeBoard()
    const tag = [...errorCatalog().values()].find((e) => e.name === 'ERR_ROUTER_KEY')!.tag
    // internal, clock 0, a chain of one: the tag (u32), the length of its data, the key 5
    board.respond = (r, send) => send(answer(r, [defs.errorRecord.srcInternal, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, tag & 0xff, (tag >> 8) & 0xff, (tag >> 16) & 0xff, tag >>> 24, 0x01, 0x05], false))
    const link = await linkTo(board)
    const error = await link.request(ping()).catch((e: unknown) => e)
    expect(error).toBeInstanceOf(CommandError)
    expect((error as CommandError).code).toBe('device-error')
    expect((error as CommandError).record?.cause?.info?.name).toBe('ERR_ROUTER_KEY')
    expect((error as CommandError).message).toMatch(/ERR_ROUTER_KEY/)
    await link.close()
  })

  it('times out when nothing answers, and a late answer is only observed', async () => {
    const board = new FakeBoard()
    let last: (typeof board.requests)[number] | undefined
    board.respond = (r) => void (last = r)
    const link = await linkTo(board, { timeoutMs: 20 })
    const events: string[] = []
    link.commands.observe((e) => events.push(e.type))
    await expect(link.request(ping())).rejects.toMatchObject({ code: 'timeout' })
    board.notify(answer(last!, [9]))
    await until(() => events.includes('unmatched'))
    expect(events).toEqual(['sent', 'timeout', 'unmatched'])
    await link.close()
  })

  it('refuses a request over the frame limit without sending it', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    await expect(link.request(ping(new Uint8Array(defs.frameMax - defs.frameHead - 1)))).rejects.toMatchObject({ code: 'too-long' })
    expect(board.requests).toHaveLength(0)
    await link.close()
  })

  it('the packets of two requests never interleave on the wire', async () => {
    const board = new FakeBoard()
    board.writeDelayMs = 1
    echo(board)
    const link = await linkTo(board)
    const a = new Uint8Array(400).fill(0xaa)
    const b = new Uint8Array(400).fill(0xbb)
    const [ra, rb] = await Promise.all([link.request(ping(a)), link.request(ping(b))])
    expect(ra).toEqual(a)
    expect(rb).toEqual(b)
    await link.close()
  })

  it('an urgent request goes before the queued ones and past a full window, with the published flag', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board, { maxInFlight: 2, timeoutMs: 1000 })
    const normal = [1, 2, 3, 4].map((n) => link.request(ping([n])).catch(() => undefined)) // never answered
    await until(() => board.requests.length === 2) // the window is full, two wait
    void link.request(ping([], { urgent: true })).catch(() => undefined)
    await until(() => board.requests.length === 3)
    expect(board.requests[2]!.flags).toBe(defs.flags.urgent)
    await link.close()
    await Promise.all(normal)
  })

  it('a NOREPLY request is done once written: no wait for an answer that never comes, no timeout, no stall', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board, { timeoutMs: 20, stallAfter: 1 })
    const events: string[] = []
    link.commands.observe((e) => events.push(e.type))
    for (let i = 0; i < 5; i++) expect([...(await link.request(ping([], { noReply: true })))]).toEqual([])
    expect(board.requests.map((r) => r.flags)).toEqual(Array(5).fill(defs.flags.noReply))
    await new Promise((r) => setTimeout(r, 60))
    expect(events.filter((e) => e === 'timeout')).toEqual([])
    expect(link.state).toBe('connected')
    expect(link.commands.pending).toBe(0)
    await link.close()
  })
})

describe('streams', () => {
  it('a PUSH is its key\'s stream: errors and events on the session, logs and watches on their raw characteristics', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    const seen: [string | undefined, number[], number | undefined][] = []
    link.onStream((f) => seen.push([f.name, [...f.data], f.seq]))
    board.push(defs.streams.errors, [7], { seq: 4 })
    board.push(defs.streams.events, [5], { seq: 5 })
    await until(() => seen.length === 2)
    board.push(defs.streams.logs, [...new TextEncoder().encode('I (1) x: y')], { characteristic: defs.gatt.logs, src: defs.src.none })
    await until(() => seen.length === 3)
    board.push(defs.streams.watches, [CLIENT_ID, 1, defs.status.ok, 9], { characteristic: defs.gatt.telemetry, src: defs.src.none })
    await until(() => seen.length === 4)
    expect(seen).toEqual([
      ['errors', [7], 4],
      ['events', [5], 5],
      ['logs', [...new TextEncoder().encode('I (1) x: y')], undefined], // a raw characteristic has no session count
      ['watches', [CLIENT_ID, 1, defs.status.ok, 9], undefined],
    ])
    await link.close()
  })

  it('a push says who made it and whether it was urgent', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    const seen: [number, boolean][] = []
    link.onStream((f) => seen.push([f.src, f.urgent]))
    board.push(defs.streams.errors, [1], { urgent: true, src: 7 })
    await until(() => seen.length === 1)
    expect(seen).toEqual([[7, true]])
    await link.close()
  })

  it('a key that is no stream, a broken frame and a raw notification shorter than a header are diagnostics', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    const diagnostics: string[] = []
    link.onDiagnostic((d) => diagnostics.push(d.kind))
    board.push(defs.keys.sys!, [1])
    await until(() => diagnostics.length === 1)
    board.notify(Uint8Array.of(1, 2), defs.gatt.logs)
    await until(() => diagnostics.length === 2)
    ;(link as unknown as { onPacket(c: number, p: Uint8Array): void }).onPacket(defs.gatt.session, Uint8Array.of(0x05, 1, 2)) // a packet outside a frame
    expect(diagnostics).toEqual(['unknown-stream', 'not-a-frame', 'dropped'])
    await link.close()
  })
})

describe('the session', () => {
  it('a NO_SESSION fails the requests on the way and opens the session again', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    const waiting = link.request(ping()).catch((e: unknown) => e)
    await until(() => board.requests.length === 1)
    board.notify(board.control(defs.control.noSession))
    expect(((await waiting) as CommandError).code).toBe('link-lost')
    await until(() => link.state === 'connected')
    expect(board.opens).toBe(1) // the same link
    await link.close()
  })

  it('a HELLO with another boot id is a restart: the session opens again with the new one', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    const seen: LinkStatus[] = []
    link.onStatus((s) => seen.push(s))
    board.bootId = 2
    board.notify(board.hello())
    await until(() => seen.some((s) => s.state === 'connected'))
    expect(seen[0]).toMatchObject({ state: 'reconnecting', reason: 'The board restarted.' })
    expect(link.session?.bootId).toBe(2)
    await link.close()
  })

  it('with the board\'s dead-man on, a silent link PINGs', async () => {
    const board = new FakeBoard()
    board.deadmanMs = 150
    const link = await linkTo(board)
    expect(link.session?.deadmanMs).toBe(150)
    await until(() => board.pings >= 2)
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
    const waiting = link.request(ping()).catch((e: unknown) => e) // nobody answers
    await until(() => board.requests.length === 1)
    board.drop()
    expect(((await waiting) as CommandError).code).toBe('link-lost')
    await until(() => link.state === 'connected')
    expect(board.opens).toBe(2)
    expect(seen.map((s) => s.state)).toEqual(['reconnecting', 'connected'])
    expect(seen[0]).toMatchObject({ reason: 'out of range', attempt: 0 })
    echo(board)
    expect((await link.request(ping())).length).toBe(0) // and it works again
    await link.close()
  })

  it('a request given while the link is coming back waits for it', async () => {
    const board = new FakeBoard()
    echo(board)
    const link = await linkTo(board)
    board.failOpens = 2
    board.drop()
    expect([...(await link.request(ping([42])))]).toEqual([42])
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

  it('disconnect() stops the reconnecting, and a request then fails not-connected', async () => {
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
    await expect(link.request(ping())).rejects.toMatchObject({ code: 'not-connected' })
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
    const results = await Promise.allSettled([link.request(ping()), link.request(ping())])
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

  it('a first connect with retry is shown as connecting, never as a lost link', async () => {
    const board = new FakeBoard()
    board.failOpens = 2
    const link = new RunitLink(board, { reconnect: { delaysMs: [0, 1] } })
    const seen: string[] = []
    link.onStatus((s) => seen.push(s.state))
    await link.connect(board.device, { retry: true })
    expect(seen).not.toContain('reconnecting')
    expect(seen.at(-1)).toBe('connected')
    await link.close()
  })

  it('disconnect() while an attempt is under way leaves the link idle when that attempt fails', async () => {
    const board = new FakeBoard()
    const link = new RunitLink(board, { reconnect: { delaysMs: [0], openTimeoutMs: 50 } })
    board.hangOpens = 1
    const connecting = link.connect(board.device, { retry: true }).catch(() => undefined)
    await until(() => board.opens === 1)
    await link.disconnect()
    await new Promise((r) => setTimeout(r, 120)) // the hung attempt times out now
    expect(link.state).toBe('idle')
    board.release()
    await connecting
    await link.close()
  })

  it('a write that fails fails its request with send-failed and recycles the link (the OS reports no disconnect after a GATT change)', async () => {
    const board = new FakeBoard()
    echo(board)
    const link = await linkTo(board)
    board.failNextWrite = new Error('The object has been closed')
    await expect(link.request(ping())).rejects.toMatchObject({ code: 'send-failed' })
    await until(() => board.opens === 2 && link.state === 'connected')
    expect(board.closes).toBe(1)
    expect((await link.request(ping())).length).toBe(0)
    await link.close()
  })

  it('a packet is never longer than the 512 B ATT value', async () => {
    const board = new FakeBoard()
    echo(board)
    const link = await linkTo(board, { packetBytes: 4000 })
    await link.request(ping(new Uint8Array(500)))
    expect(Math.max(...board.packets)).toBeLessThanOrEqual(512)
    await link.close()
  })
})
