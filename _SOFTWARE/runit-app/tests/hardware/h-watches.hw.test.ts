import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { serviceCall, Watches } from '@backend'
import { openBoard, sleep, until } from './harness'
import type { Board } from './harness'

/* The watches the app's views use (SYS_WATCH.MD): one request, then the board sends the answers by itself. */

let board: Board
let watches: Watches
beforeAll(async () => {
  board = await openBoard()
  watches = new Watches(board.link)
})
afterAll(async () => {
  watches.close()
  await board.close()
})

describe('following a read', () => {
  it('VM STATUS every second arrives with no request after the first, and stops when the watch is removed', async () => {
    const seen: unknown[] = []
    const stop = await watches.follow(serviceCall(board.services.catalog, 'VM', 'STATUS', undefined, 'read'), { trigger: 'every', periodMs: 200 }, (v) => seen.push(v))
    expect(await until(() => seen.length >= 5, 5000)).toBeTruthy()
    expect(seen[0]).toMatchObject({ mode: expect.anything() })
    await stop()
    await sleep(300)
    const n = seen.length
    await sleep(700)
    expect(seen.length).toBe(n)
  })

  it('an ON_CHANGE watch sends when the answer changes and anyway after its silence', async () => {
    const seen: unknown[] = []
    const stop = await watches.follow(serviceCall(board.services.catalog, 'SYS', 'SAFE', undefined, 'read'), { trigger: 'change', periodMs: 100, silenceMs: 500 }, (v) => seen.push(v))
    await sleep(1800)
    await stop()
    expect(seen.length).toBeGreaterThanOrEqual(2) // the first entry, then the silence's
    expect(seen.length).toBeLessThanOrEqual(6)
  })
})
