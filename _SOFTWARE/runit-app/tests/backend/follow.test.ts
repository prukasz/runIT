import { describe, expect, it } from 'vitest'
import { bundledServiceCatalog, RunitLink, serviceCall, WATCH_COMMANDS, Watches } from '@backend'
import type { Frame } from '@backend'
import { answer, CLIENT_ID, defs, FakeBoard, until } from './fakes'

const catalog = bundledServiceCatalog()
const watch = catalog.service(WATCH_COMMANDS.service)
const ADD = watch.commands.get(WATCH_COMMANDS.add)!.op
const REMOVE = watch.commands.get(WATCH_COMMANDS.remove)!.op

/** A watch's entry as the board sends it on the raw telemetry characteristic: `[owner][id][status][data]`. */
const entry = (board: FakeBoard, owner: number, id: number, ok: boolean, data: number[]) =>
  board.push(defs.streams.watches, [owner, id, ok ? defs.status.ok : defs.status.error, ...data], { characteristic: defs.gatt.telemetry, src: defs.src.none })

const watchOps = (board: FakeBoard, op: number): Frame[] => board.requests.filter((r) => r.key === defs.keys.watch && r.data[1] === op) // [dir][op][head: id ...]

const connected = async (timeoutMs = 200, reconnect = false) => {
  const board = new FakeBoard()
  board.respond = (r, send) => send(answer(r))
  const link = new RunitLink(board, { timeoutMs, ...(reconnect ? {} : { reconnect: false }) })
  await link.connect(board.device)
  return { board, link }
}

describe('Watches.follow', () => {
  it('adds one watch for a read and hands each of its own entries over decoded as the command answers it', async () => {
    const { board, link } = await connected()
    const watches = new Watches(link)
    const call = serviceCall(catalog, 'SYS', 'PING')
    const got: unknown[] = []
    const stop = await watches.follow(call, { trigger: 'change', periodMs: 100, silenceMs: 1000 }, (v) => got.push(v))

    const [add] = watchOps(board, ADD)
    const id = add!.data[2]!
    expect(id).toBeGreaterThan(0)
    expect(add!.data.subarray(2 + 6)).toEqual(Uint8Array.from([call.request.key, ...call.request.body])) // the read, a stored frame, after the 6 B head

    entry(board, CLIENT_ID + 1, id, true, [1, 0, 0, 0]) // another client's watch with the same id: not this one's
    entry(board, CLIENT_ID, id, true, [5, 0, 0, 0])
    entry(board, CLIENT_ID, id, false, [])
    await until(() => got.length === 2)
    expect(got[0]).toEqual({ uptime_ms: 5 })
    expect(got[1]).toBeUndefined()

    await stop()
    expect(watchOps(board, REMOVE)).toHaveLength(1)
    await link.close()
  })

  it('periods are in the board\'s ticks (SYS_WATCH_TICK_MS)', async () => {
    const { board, link } = await connected()
    await new Watches(link).follow(serviceCall(catalog, 'SYS', 'PING'), { trigger: 'every', periodMs: 200, silenceMs: 1000 }, () => undefined)
    const head = watchOps(board, ADD)[0]!.data.subarray(2, 8)
    const view = new DataView(head.buffer, head.byteOffset, head.byteLength)
    expect(view.getUint16(2, true)).toBe(200 / defs.watchTickMs)
    expect(view.getUint16(4, true)).toBe(1000 / defs.watchTickMs)
    await link.close()
  })

  it('two of them on one link never take the same id', async () => {
    const { board, link } = await connected()
    const call = serviceCall(catalog, 'SYS', 'PING')
    const spec = { trigger: 'change', periodMs: 100 } as const
    await new Watches(link).follow(call, spec, () => undefined)
    await new Watches(link).follow(call, spec, () => undefined)
    expect(new Set(watchOps(board, ADD).map((r) => r.data[2])).size).toBe(2)
    await link.close()
  })

  it('close removes every watch from the board, one whose ADD is not answered yet too, and takes no more', async () => {
    const { board, link } = await connected()
    const watches = new Watches(link)
    const call = serviceCall(catalog, 'SYS', 'PING')
    const spec = { trigger: 'change', periodMs: 100 } as const
    await watches.follow(call, spec, () => undefined)
    const onItsWay = watches.follow(call, spec, () => undefined)
    watches.close()
    await onItsWay
    await until(() => watchOps(board, REMOVE).length === 2)
    expect(new Set(watchOps(board, REMOVE).map((r) => r.data[2]))).toEqual(new Set(watchOps(board, ADD).map((r) => r.data[2])))
    await expect(watches.follow(call, spec, () => undefined)).rejects.toThrow(/closed/)
    expect(watchOps(board, ADD)).toHaveLength(2)
    await link.close()
  })

  it('a watch stopped while the link is lost is not removed on the board when it comes back: its session took it', async () => {
    const { board, link } = await connected(500, true)
    const stop = await new Watches(link).follow(serviceCall(catalog, 'SYS', 'PING'), { trigger: 'change', periodMs: 100 }, () => undefined)
    board.drop()
    await stop()
    await until(() => link.state === 'connected', 3000)
    await new Promise((r) => setTimeout(r, 100))
    expect(watchOps(board, REMOVE)).toHaveLength(0)
    await link.close()
  })
})
