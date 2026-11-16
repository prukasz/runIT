import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { decodeErrorRecord, decodeEvent } from '@backend'
import type { BoardEvent, StreamFrame } from '@backend'
import { openBoard, until } from './harness'
import type { Board } from './harness'

/*
 * What the board sends by itself, as the setup of app/runit routes it (RUNIT.MD): the errors and the events that matter to the
 * user to this client's session as PUSH frames, and nothing subscribed (the log lines, text on their own characteristic, are held by link.test).
 */

let board: Board
const frames: StreamFrame[] = []
let off: () => void
beforeAll(async () => {
  board = await openBoard()
  off = board.link.onStream((f) => frames.push(f))
})
afterAll(async () => {
  off()
  await board.services.call('SYS', 'SAFE_LEAVE').catch(() => undefined)
  await board.close()
})

describe('the session as the board opened it', () => {
  it('the hello gave the board its id, its boot and its build, and no stream was subscribed', () => {
    const session = board.link.session!
    expect(session.clientId).toBe(board.link.defs.session.id.app)
    expect(session.boardId).toBeLessThanOrEqual(board.link.defs.session.id.boardLast)
    expect(session.bootId).toBeGreaterThan(0)
    expect(session.buildId).toBeGreaterThan(0)
  })
})

describe('events', () => {
  it('a device suspended and resumed are events of the DEVICE contract, named by its table, with the source the device', async () => {
    const events: BoardEvent[] = []
    const stop = board.link.on('events', (data) => {
      const event = decodeEvent(board.services.catalog, data)
      if (event) events.push(event)
    })
    await board.services.device(0, 'DEVICE', 'SUSPEND')
    await board.services.device(0, 'DEVICE', 'RESUME')
    expect(await until(() => events.some((e) => e.event?.id === 'RESUMED'), 3000)).toBe(true)
    stop()
    const suspended = events.find((e) => e.event?.id === 'SUSPENDED')
    expect(suspended).toMatchObject({ source: 0, contract: { id: 'DEVICE' } })
    expect(suspended!.tMs).toBeGreaterThan(0)
  })
})

describe('errors', () => {
  it('an error is on the errors stream as the record the answer carried', async () => {
    const before = frames.length
    const error = await board.services.device(0, 'IO', 'LEVEL', { pin: 99, level: 'High' }, { side: 'write' }).then(() => undefined, (e: unknown) => e as { record?: { chain: unknown[] } })
    const pushed = await until(() => frames.slice(before).find((f) => f.name === 'errors'), 3000)
    expect(pushed).toBeDefined()
    expect(decodeErrorRecord(board.link.defs, pushed!.data)?.chain.length).toBe(error?.record?.chain.length)
  })
})
