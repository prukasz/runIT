import { appendFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'
import { sendInOrder } from '@backend'
import { defs, openBoard, sleep } from './harness'

/*
 * A link held past the link layer's 40 s response timeout, with data on it all the time: a procedure that is never answered
 * drops the link at 40 s (LL response timeout, SYS_BLE.MD), which a shorter test does not see. HW_SOAK_S sets the time.
 * HW_SPEED_LOG appends the figures to a file. HW_UART=none leaves the console port to something else.
 */

const SECONDS = Number(process.env.HW_SOAK_S ?? 46)
const ID = 0x00000ffe
const RECORD = 4000
const CHUNK = 480
const u32 = (n: number) => Uint8Array.of(n & 0xff, (n >>> 8) & 0xff, (n >>> 16) & 0xff, n >>> 24)
const keys = (op: string) => [defs.keys.actions!, defs.dir.write, defs.ops.actions[op]!]

describe('a link past the 40 s', () => {
  it('stays up with data on it', async () => {
    const board = await openBoard()
    const started = Date.now()
    let bytes = 0
    let failure = ''
    try {
      while (Date.now() - started < SECONDS * 1000) {
        await board.link.request({ keys: keys('BEGIN'), payload: Uint8Array.of(...u32(ID), RECORD & 0xff, RECORD >> 8) }, { timeoutMs: 2000 })
        const writes = []
        for (let at = 0; at < RECORD; at += CHUNK) {
          const data = new Uint8Array(Math.min(CHUNK, RECORD - at)).fill(0x55)
          writes.push({ keys: keys('WRITE'), payload: Uint8Array.of(at & 0xff, at >> 8, ...data) })
          bytes += data.length
        }
        await sendInOrder(board.link, writes, { timeoutMs: 2000 })
        await board.link.request({ keys: keys('ABORT') }, { timeoutMs: 2000 })
        await sleep(100) // room for what else a board does
      }
    } catch (error) {
      failure = error instanceof Error ? error.message : String(error)
    }
    const seconds = (Date.now() - started) / 1000
    const drops = board.statuses.filter((s) => s.state !== 'connected' && s.state !== 'connecting').length
    const line = `soak: ${seconds.toFixed(1)} s, ${(bytes / 1024 / seconds).toFixed(1)} KB/s, drops ${drops}${failure ? `, failed: ${failure}` : ''}`
    if (process.env.HW_SPEED_LOG) appendFileSync(process.env.HW_SPEED_LOG, `${line}\n`)
    await board.close()
    expect(failure).toBe('')
    expect(drops).toBe(0)
  }, (SECONDS + 15) * 1000)
})
