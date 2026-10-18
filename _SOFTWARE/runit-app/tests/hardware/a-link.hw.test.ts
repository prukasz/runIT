import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { CommandError } from '@backend'
import { defs, openBoard, until } from './harness'
import type { Board } from './harness'

let board: Board
beforeAll(async () => {
  board = await openBoard()
})
afterAll(async () => {
  await board.close()
})

describe('the link on a real board', () => {
  it('connects and answers PING through the catalog', async () => {
    expect(board.link.state).toBe('connected')
    const { value } = await board.services.call('SYS', 'PING')
    expect((value as { uptime_ms: number }).uptime_ms).toBeGreaterThan(0)
  })

  it('the MTU is what the board offers', () => {
    expect(board.platform.mtu).toBeGreaterThanOrEqual(247)
  })

  it('50 PINGs in a row, then 40 pipelined, all answered', async () => {
    for (let i = 0; i < 50; i++) await board.services.call('SYS', 'PING')
    const answers = await Promise.all(Array.from({ length: 40 }, () => board.services.call('SYS', 'PING')))
    expect(answers).toHaveLength(40)
  })

  it('an unknown router key is a CommandError with the error record decoded', async () => {
    const error = await board.link.request({ keys: [0xee] }).then(
      () => undefined,
      (e: unknown) => e,
    )
    expect(error).toBeInstanceOf(CommandError)
    expect((error as CommandError).code).toBe('device-error')
    expect((error as CommandError).record?.chain.length).toBeGreaterThan(0)
  })

  it('the same error also arrives on the errors stream', async () => {
    const seen: Uint8Array[] = []
    const off = board.link.on('errors', (d) => seen.push(d))
    await board.link.request({ keys: [0xee] }).catch(() => undefined)
    expect(await until(() => seen.length > 0, 2000)).toBe(true)
    off()
  })

  it('answers of any size: LIST of the BLE router arrives whole', async () => {
    const { data } = await board.services.call('BLE', 'LIST')
    expect(data.length).toBeGreaterThan(0)
  })

  it('a link that the radio lost comes back by itself, and the next command works', async () => {
    const before = board.statuses.length
    await board.platform.dropFromHost()
    expect(await until(() => board.statuses.slice(before).some((s) => s.state === 'reconnecting'), 3000)).toBe(true)
    expect(await until(() => board.link.state === 'connected', 10_000)).toBe(true)
    await board.services.call('SYS', 'PING')
  })

  it('10 drops, each followed by a command', async () => {
    for (let i = 0; i < 10; i++) {
      await board.platform.dropFromHost()
      await board.services.call('SYS', 'PING') // waits for the link to come back
    }
  })

  it('reopen() picks the link up again', async () => {
    await board.link.reopen()
    await board.services.call('SYS', 'PING')
  })

  it('a board restart: the loss is noticed, the link comes back, the uptime starts over', async () => {
    const uptime = async () => ((await board.services.call('SYS', 'PING')).value as { uptime_ms: number }).uptime_ms
    const up = await uptime()
    const before = board.statuses.length
    await board.services.call('SYS', 'RESTART') // answered, then the board restarts about 100 ms later
    const restarted = await until(async () => {
      try {
        const now = await uptime()
        return now < up ? now : undefined
      } catch {
        return undefined
      }
    }, 25_000, 250)
    expect(restarted).toBeDefined()
    expect(board.statuses.slice(before).some((s) => s.state === 'reconnecting')).toBe(true)
    expect(board.link.state).toBe('connected')
  })

  it('the console logged no panic', () => {
    expect(board.console.trouble(board.startedAt)).toEqual([])
  })

  it('knows its own protocol numbers', () => {
    expect(defs.gatt.service).toBeGreaterThan(0)
  })
})
