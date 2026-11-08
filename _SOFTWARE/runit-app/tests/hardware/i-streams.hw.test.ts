import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { Sessions, Streams } from '@backend'
import { defs, openBoard } from './harness'
import type { Board } from './harness'

/* The session as the firmware opens it (each side's own id in its hello) and the STREAM and SESSION routers, on a board. */

let board: Board
let streams: Streams
let sessions: Sessions
beforeAll(async () => {
  board = await openBoard()
  streams = new Streams(board.link, board.services)
  sessions = new Sessions(board.link, board.services)
})
afterAll(async () => {
  streams.close()
  await board.close()
})

const USER = 0x20

describe('the session and the streams on a real board', () => {
  it('the session opened with the app\'s own id; the board said its id and boot id in its hello', async () => {
    const s = board.link.session!
    expect(s.clientId).toBe(defs.session.id.app)
    expect(s.bootId).not.toBe(0)
    expect(s.timeoutMs).toBeGreaterThanOrEqual(150)
    expect(await sessions.boardId()).toBe(s.boardId)
  })

  it('PEERS lists this client\'s session: open, its id, on the BLE link', async () => {
    const peers = await sessions.peers()
    const mine = peers.find((p) => p.peerId === defs.session.id.app)
    expect(mine).toMatchObject({ kind: 'client', open: true })
    expect(mine!.provider).toBe(3) // RUNIT_BOARD_PROVIDER_BLE_RUNIT
  })

  it('LIST has the board\'s four streams, OUT, its own; errors, telemetry and events have this client as a subscriber', async () => {
    const list = await streams.list()
    for (const id of Object.values(defs.streams)) expect(list.find((s) => s.id === id), `stream ${id}`).toMatchObject({ dir: 'out', system: true })
    for (const id of [defs.streams.errors, defs.streams.telemetry, defs.streams.events]) {
      expect(list.find((s) => s.id === id)!.targets.some((t) => t.kind === 'peer' && t.peer === defs.session.id.app), `subscribed to ${id}`).toBe(true)
    }
  })

  it('a user stream: made, subscribed (a target of LIST), unsubscribed, removed', async () => {
    await streams.remove(USER).catch(() => undefined) // left by a run that failed
    await streams.create(USER, { dir: 'out' })
    let s = (await streams.list()).find((x) => x.id === USER)
    expect(s).toMatchObject({ dir: 'out', system: false, targets: [] })
    const stop = await streams.subscribe(USER, () => undefined)
    s = (await streams.list()).find((x) => x.id === USER)
    expect(s!.targets).toEqual([{ kind: 'peer', peer: defs.session.id.app, dropped: 0 }])
    await stop()
    s = (await streams.list()).find((x) => x.id === USER)
    expect(s!.targets).toEqual([])
    await streams.remove(USER)
    expect((await streams.list()).some((x) => x.id === USER)).toBe(false)
  })

  it('the board\'s own streams refuse REMOVE', async () => {
    await expect(streams.remove(defs.streams.errors)).rejects.toMatchObject({ code: 'device-error' })
  })

  it('an IN stream is made the other way', async () => {
    await streams.create(USER, { dir: 'in' })
    expect((await streams.list()).find((x) => x.id === USER)).toMatchObject({ dir: 'in' })
    await streams.remove(USER)
  })
})
