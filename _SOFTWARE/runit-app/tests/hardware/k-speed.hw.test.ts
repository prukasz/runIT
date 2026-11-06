import { appendFileSync } from 'node:fs'
import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { readRecord, removeRecord, sendInOrder, storeRecord } from '@backend'
import { defs, openBoard } from './harness'
import type { Board } from './harness'

/*
 * How fast the link carries data, with no flash in the way: records of the actions router are built in RAM (BEGIN, WRITE, ABORT
 * stores nothing). HW_SPEED_LOG appends the figures to a file.
 */

const RECORD = 4000
const CHUNK = 480
const ROUNDS = 8
const ID = 0x00000fff
const u32 = (n: number) => Uint8Array.of(n & 0xff, (n >>> 8) & 0xff, (n >>> 16) & 0xff, n >>> 24)
const keys = (op: string) => [defs.keys.actions!, defs.dir.write, defs.ops.actions[op]!]

describe('the speed of the link', () => {
  let board: Board
  beforeAll(async () => {
    board = await openBoard({ maxInFlight: 32 })
  })
  afterAll(async () => {
    await board.close()
  })

  const log = (line: string) => {
    if (process.env.HW_SPEED_LOG) appendFileSync(process.env.HW_SPEED_LOG, `${line}\n`)
  }

  it('the round trip of a command', async () => {
    const times: number[] = []
    for (let i = 0; i < 200; i++) {
      const started = performance.now()
      await board.services.call('SYS', 'PING')
      times.push(performance.now() - started)
    }
    times.sort((a, b) => a - b)
    log(`ping: median ${times[100]!.toFixed(1)} ms, p95 ${times[190]!.toFixed(1)} ms, min ${times[0]!.toFixed(1)} ms, max ${times[199]!.toFixed(1)} ms`)
  })

  it('from the board: a record read back', async () => {
    const frames = Array.from({ length: 8 }, () => new Uint8Array(490).fill(0x55))
    await storeRecord(board.link, ID, frames)
    const started = Date.now()
    let bytes = 0
    for (let round = 0; round < 8; round++) {
      for (const frame of await readRecord(board.link, ID)) bytes += frame.length
    }
    const seconds = (Date.now() - started) / 1000
    await removeRecord(board.link, ID)
    log(`read: ${(bytes / 1024 / seconds).toFixed(1)} KB/s, ${bytes} B in ${seconds.toFixed(2)} s`)
  })

  it.each([1, 4, 16])('window %i', async (window) => {
    const started = Date.now()
    let bytes = 0
    for (let round = 0; round < ROUNDS; round++) {
      await board.link.request({ keys: keys('BEGIN'), payload: Uint8Array.of(...u32(ID), RECORD & 0xff, RECORD >> 8) })
      const writes = []
      for (let at = 0; at < RECORD; at += CHUNK) {
        const data = new Uint8Array(Math.min(CHUNK, RECORD - at)).fill(0x55)
        writes.push({ keys: keys('WRITE'), payload: Uint8Array.of(at & 0xff, at >> 8, ...data) })
        bytes += data.length
      }
      await sendInOrder(board.link, writes, { window, timeoutMs: 10_000 })
      await board.link.request({ keys: keys('ABORT') })
    }
    const seconds = (Date.now() - started) / 1000
    const line = `window ${window}: ${(bytes / 1024 / seconds).toFixed(1)} KB/s, ${bytes} B in ${seconds.toFixed(2)} s`
    if (process.env.HW_SPEED_LOG) appendFileSync(process.env.HW_SPEED_LOG, `${line}\n`)
    expect(bytes).toBeGreaterThan(0)
  })
})
