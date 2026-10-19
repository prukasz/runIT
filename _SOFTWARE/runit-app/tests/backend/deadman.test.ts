import { describe, expect, it } from 'vitest'
import { bundledServiceCatalog, DEADMAN_COMMANDS, DeadMan, RunitLink } from '@backend'
import type { DeadManEvent } from '@backend'
import { answer, defs, FakeBoard, until } from './fakes'

const catalog = bundledServiceCatalog()
const sys = catalog.service(DEADMAN_COMMANDS.service)
const SET = sys.commands.get(DEADMAN_COMMANDS.set)!.op
const PING = sys.commands.get(DEADMAN_COMMANDS.ping)!.op
const u32 = (n: number) => [n & 0xff, (n >>> 8) & 0xff, (n >>> 16) & 0xff, n >>> 24]

/** A board that answers the DEADMAN write and PING (with `uptime`), and holds every other request unanswered when `hold`. */
const board = () => {
  const b = Object.assign(new FakeBoard(), { uptime: 1000, hold: false })
  b.respond = (frame, send) => {
    if (frame[2] !== defs.keys.sys) return b.hold ? undefined : send(answer(frame[0]!))
    if (frame[4] === PING) send(answer(frame[0]!, u32((b.uptime += 10))))
    else send(answer(frame[0]!))
  }
  return b
}

const sysOps = (b: FakeBoard, op: number) => b.requests.filter((f) => f[2] === defs.keys.sys && f[4] === op) // [seq][flags][key][dir][op]

const start = async (b: FakeBoard, options: { timeoutMs?: number; onLinkLost?: boolean; heartbeatMs?: number } = {}) => {
  const link = new RunitLink(b, { timeoutMs: 200, reconnect: { delaysMs: [0, 1, 2], openTimeoutMs: 50 } })
  await link.connect(b.device)
  const deadman = new DeadMan(link, { timeoutMs: 120, heartbeatMs: 30, ...options })
  const events: DeadManEvent[] = []
  deadman.onEvent((e) => events.push(e))
  await deadman.start()
  return { link, deadman, events }
}

describe('the dead-man', () => {
  it('names commands and fields the descriptor has', () => {
    expect(sys.commands.get(DEADMAN_COMMANDS.set)!.write!.args!.fields.map((f) => f.id)).toEqual([DEADMAN_COMMANDS.timeout, DEADMAN_COMMANDS.onLinkLost])
    const ping = sys.commands.get(DEADMAN_COMMANDS.ping)!.read!.returns
    expect(ping?.kind === 'struct' && ping.layout.fields.map((f) => f.id)).toEqual([DEADMAN_COMMANDS.uptime])
  })

  it('is armed when it starts: the DEADMAN write (urgent) with the timeout and the link-lost flag, then PING', async () => {
    const b = board()
    const { link, deadman, events } = await start(b)
    const set = sysOps(b, SET)[0]!
    expect(set[1]).toBe(defs.flags.urgent)
    expect(set[3]).toBe(defs.dir.write)
    expect([...set.subarray(5)]).toEqual([...u32(120), 1])
    expect(sysOps(b, PING)).toHaveLength(1)
    expect(events).toEqual([{ type: 'armed', timeoutMs: 120, onLinkLost: true }])
    await deadman.stop()
    await link.close()
  })

  it('beats only when nothing else was written', async () => {
    const b = board()
    const { link, deadman } = await start(b)
    const pings = sysOps(b, PING).length
    await until(() => sysOps(b, PING).length >= pings + 2, 500)
    const busy = sysOps(b, PING).length
    for (let i = 0; i < 12; i++) {
      await link.request({ keys: [defs.keys.i2c!, 1] })
      await new Promise((resolve) => setTimeout(resolve, 10))
    }
    expect(sysOps(b, PING).length).toBeLessThanOrEqual(busy + 1) // traffic every 10 ms feeds it: at most one beat in flight
    await deadman.stop()
    await link.close()
  })

  it('beats past commands the board does not answer', async () => {
    const b = board()
    const { link, deadman } = await start(b)
    b.hold = true
    const held = Array.from({ length: 8 }, () => link.request({ keys: [defs.keys.i2c!, 1] }).catch(() => undefined))
    const pings = sysOps(b, PING).length
    await until(() => sysOps(b, PING).length >= pings + 2, 500)
    await deadman.stop({ disarm: false })
    await link.close()
    await Promise.all(held)
  })

  it('is armed again after a reconnect, and says the drop put the board in its safe state', async () => {
    const b = board()
    const { link, deadman, events } = await start(b)
    b.drop()
    await until(() => events.some((e) => e.type === 'lapsed'), 500)
    expect(sysOps(b, SET)).toHaveLength(2)
    expect(events.find((e) => e.type === 'lapsed')).toMatchObject({ reason: 'link-lost' })
    await deadman.stop()
    await link.close()
  })

  it('tells a board that restarted from one that only lost the link', async () => {
    const b = board()
    const { link, deadman, events } = await start(b)
    b.uptime = 5
    b.drop()
    await until(() => events.some((e) => e.type === 'restarted'), 500)
    expect(events.some((e) => e.type === 'lapsed')).toBe(false)
    await deadman.stop()
    await link.close()
  })

  it('without link-lost, a short drop is no lapse', async () => {
    const b = board()
    const { link, deadman, events } = await start(b, { onLinkLost: false, timeoutMs: 600, heartbeatMs: 100 })
    b.drop()
    await until(() => sysOps(b, SET).length === 2 && sysOps(b, PING).length >= 2, 500)
    await new Promise((resolve) => setTimeout(resolve, 20))
    expect(events.filter((e) => e.type === 'lapsed')).toEqual([])
    await deadman.stop()
    await link.close()
  })

  it('stop turns the board dead-man off (timeout 0)', async () => {
    const b = board()
    const { link, deadman } = await start(b)
    await deadman.stop()
    expect([...sysOps(b, SET).at(-1)!.subarray(5)]).toEqual([...u32(0), 0])
    expect(deadman.active).toBe(false)
    await link.close()
  })

  it('refuses a heartbeat that cannot keep up with the timeout', () => {
    const link = new RunitLink(board())
    expect(() => new DeadMan(link, { timeoutMs: 100, heartbeatMs: 70 })).toThrow(RangeError)
  })
})
