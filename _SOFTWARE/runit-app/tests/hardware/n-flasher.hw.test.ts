import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { bundledServiceCatalog, FLASHER_STATE, flashAttached, Sessions } from '@backend'
import { backUp, openBoard } from './harness'
import type { Board } from './harness'

/*
 * The board the app is on flashes the ESP32-S3 on its link (SYS_FLASHER.MD), and that board then answers through it. Only with
 * HW_FLASHER=1: it rewrites the attached board. Wiring: this board's GPIO 18 to the other's U0RXD (its RX pin), 17 to its U0TXD (its TX pin), 46 to its
 * GPIO 0, 3 to its EN, the grounds joined; this board's id not 0 (SESSION ID, then a restart).
 */

const catalog = bundledServiceCatalog()
const NEW_BOARD = 0 // a flashed board's id until it is given another

describe.runIf(process.env.HW_FLASHER)('sys_flasher', () => {
  let board: Board
  let sessions: Sessions
  beforeAll(async () => {
    board = await openBoard()
    sessions = new Sessions(board.link, board.services)
  })
  afterAll(async () => {
    await board.close()
  })

  it('this board has an id of its own, not the new board\'s (given 1 and restarted when it has 0)', async () => {
    if (board.link.session?.boardId === NEW_BOARD) {
      await board.services.call('SESSION', 'ID', { board_id: 1 }, { side: 'write' })
      await board.services.call('SYS', 'RESTART')
      await new Promise((resolve) => setTimeout(resolve, 2000))
      expect(await backUp(board, 40_000)).toBeDefined()
    }
    expect(board.link.session?.boardId).not.toBe(NEW_BOARD)
  }, 60_000)

  it('writes its firmware into the attached board, checked region by region', async () => {
    let steps = new Set<number>()
    // The board's log lines while it flashes: what the attached chip said, kept for a failure's message.
    const lines: string[] = []
    const off = board.link.onStream((f) => {
      if (f.name === 'logs') lines.push(new TextDecoder().decode(f.data).trim())
    })
    const last = await flashAttached(board.link, catalog, { onProgress: (s) => steps.add(s.step) })
      .catch((error: unknown) => {
        throw new Error(`${String(error)}\n${lines.filter((l) => l.includes('flasher')).join('\n')}`)
      })
      .finally(off)
    console.log(lines.filter((l) => l.includes('flasher')).join('\n')) // what the attached board said as it started
    expect(last).toMatchObject({ state: FLASHER_STATE.done, error: 0 })
    expect(last.done).toBe(last.total)
    expect(last.total).toBeGreaterThan(100_000) // the bootloader, the table and a firmware: a flash really happened
    steps = new Set([...steps].filter((s) => s >= 3))
    expect(steps.size).toBeGreaterThan(0) // it was seen writing the firmware
  }, 240_000)

  it('the new board opens a session on the link and answers through this one', async () => {
    // The board's log lines meanwhile (a refused hello says why), and the last PEERS: a failure's message.
    const lines: string[] = []
    const off = board.link.onStream((f) => {
      if (f.name === 'logs') lines.push(new TextDecoder().decode(f.data).trim())
    })
    const start = Date.now()
    let open = false
    let peers: Awaited<ReturnType<Sessions['peers']>> = []
    while (!open && Date.now() - start < 20_000) {
      peers = await sessions.peers()
      open = peers.some((p) => p.kind === 'slave' && p.open && p.peerId === NEW_BOARD)
      if (!open) await new Promise((resolve) => setTimeout(resolve, 500))
    }
    off()
    expect(open, `PEERS ${JSON.stringify(peers)}\n${lines.filter((l) => /session|SESSION|ERR_|flasher/.test(l)).join('\n')}`).toBe(true)
    const { data } = await board.services.call('SYS', 'PING', undefined, { board: NEW_BOARD })
    expect(data).toBeDefined()
  }, 30_000)
})
