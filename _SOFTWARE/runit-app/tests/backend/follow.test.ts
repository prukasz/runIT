import { describe, expect, it } from 'vitest'
import { bundledServiceCatalog, RunitLink, serviceCall, Watches } from '@backend'
import { answer, CLIENT_ID, defs, FakeBoard, until } from './fakes'

/** A watch entry as the board sends it: a PUSH frame on the session's channel [WATCHES][owner][id][status][data]. */
const entry = (board: FakeBoard, id: number, ok: boolean, data: number[], seq: number) =>
  board.stream(defs.channels.watches, [CLIENT_ID, id, ok ? 0 : 1, ...data], seq)

describe('Watches.follow', () => {
  it('adds one watch for a read and hands each answer over decoded as the command answers it', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!))
    const link = new RunitLink(board, { timeoutMs: 200, reconnect: false })
    await link.connect(board.device)
    const watches = new Watches(link)
    const call = serviceCall(bundledServiceCatalog(), 'SYS', 'PING')
    const got: unknown[] = []
    const stop = await watches.follow(call, { trigger: 'change', periodMs: 100, silenceMs: 1000 }, (v) => got.push(v))

    const add = board.requests.find((r) => r[2] === defs.keys.watch)!
    expect(add).toBeDefined()
    const id = add[5]! // [seq][flags][key][dir][op][head: id ...]
    expect(id).toBeGreaterThan(0)

    entry(board, id, true, [5, 0, 0, 0], 0)
    entry(board, id, false, [], 1)
    await until(() => got.length === 2)
    expect(got[0]).toMatchObject({})
    expect(got[0]).not.toBeUndefined()
    expect(got[1]).toBeUndefined()

    await stop()
    expect(board.requests.some((r) => r[2] === defs.keys.watch && r[4] === 2)).toBe(true) // REMOVE
    await link.close()
  })

  it('two of them on one link never take the same id', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!))
    const link = new RunitLink(board, { timeoutMs: 200, reconnect: false })
    await link.connect(board.device)
    const call = serviceCall(bundledServiceCatalog(), 'SYS', 'PING')
    const spec = { trigger: 'change', periodMs: 100 } as const
    await new Watches(link).follow(call, spec, () => undefined)
    await new Watches(link).follow(call, spec, () => undefined)
    const ids = board.requests.filter((r) => r[2] === defs.keys.watch && r[4] === 1).map((r) => r[5])
    expect(new Set(ids).size).toBe(2)
    await link.close()
  })

  it('a watch stopped while the link is lost is not removed on the board when it comes back: its session took it', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!))
    const link = new RunitLink(board, { timeoutMs: 500 })
    await link.connect(board.device)
    const watches = new Watches(link)
    const stop = await watches.follow(serviceCall(bundledServiceCatalog(), 'SYS', 'PING'), { trigger: 'change', periodMs: 100 }, () => undefined)
    board.drop()
    const before = board.requests.length
    await stop()
    await until(() => link.state === 'connected', 3000)
    await new Promise((r) => setTimeout(r, 100))
    expect(board.requests.slice(before).some((r) => r[2] === defs.keys.watch && r[4] === 2)).toBe(false)
    await link.close()
  })
})
