import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { CommandError, DeadMan, leaveSafeState } from '@backend'
import type { DeadManEvent } from '@backend'
import { openBoard, sleep, until } from './harness'
import type { Board } from './harness'

/* The dead-man and the safe state, with the console as the witness: the board sends nothing when it enters its safe state, so
 * what the app infers (`lapsed`) is held against what the firmware logged ("safe state (reason N)"). */

const SAFE = /safe state/i
let board: Board
beforeAll(async () => {
  board = await openBoard()
})
afterAll(async () => {
  await board.services.call('SYS', 'DEADMAN_SET', { timeout_ms: 0, on_link_lost: false }).catch(() => undefined)
  await leaveSafeState(board.link).catch(() => undefined)
  await board.close()
})

const setDeadman = (timeout_ms: number, on_link_lost: boolean) => board.services.call('SYS', 'DEADMAN_SET', { timeout_ms, on_link_lost })
const getDeadman = async () => (await board.services.call('SYS', 'DEADMAN_GET')).value as { timeout_ms: number; on_link_lost: boolean }
const suspended = async (device: number) => ((await board.services.device(device, 'DEVICE', 'INFO')).value as { suspended: number }).suspended
const events = (deadman: DeadMan) => {
  const seen: DeadManEvent[] = []
  deadman.onEvent((e) => seen.push(e))
  return seen
}

describe('the dead-man kept by the app', () => {
  it('arms the board, feeds it past its timeout, and disarms it', async () => {
    const deadman = new DeadMan(board.link, { timeoutMs: 1500 })
    const seen = events(deadman)
    const since = Date.now()
    await deadman.start()
    expect(await getDeadman()).toEqual({ timeout_ms: 1500, on_link_lost: true })
    expect(seen[0]).toMatchObject({ type: 'armed', timeoutMs: 1500 })
    await sleep(4500) // three timeouts of silence from the caller: the heartbeat alone keeps the board out of the safe state
    expect(board.console.since(since, SAFE)).toEqual([])
    expect(seen.filter((e) => e.type === 'lapsed' || e.type === 'failed')).toEqual([])
    expect(await suspended(4)).toBe(0)
    await deadman.stop()
    expect((await getDeadman()).timeout_ms).toBe(0)
  })

  it('a timeout the heartbeat cannot keep is refused', () => {
    expect(() => new DeadMan(board.link, { timeoutMs: 1500, heartbeatMs: 1500 }).start()).toThrow()
  })
})

describe('the board enters its safe state', () => {
  it('after a silence of its timeout: logged, devices suspended, the board and the link stay up', async () => {
    const since = Date.now()
    await setDeadman(600, false)
    await sleep(1800) // no frame at all
    expect(await board.console.waitFor(SAFE, since, 3000)).toBeDefined()
    expect(await suspended(4)).toBe(1)
    const error = await board.services.device(4, 'IO', 'SET_VOLTAGE', { pin: 0, voltage_mV: 100 }).then(
      () => undefined,
      (e: unknown) => e,
    )
    expect(error).toBeInstanceOf(CommandError)
    expect((error as CommandError).record!.cause!.info?.name).toBe('ERR_DEV_SUSPENDED')
  })

  it('leaveSafeState resumes every device (RESUME to all)', async () => {
    await setDeadman(0, false)
    await leaveSafeState(board.link)
    for (const id of [0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13]) expect(await suspended(id), `device ${id}`).toBe(0)
  })

  it('STOP puts it there at once, and the same way out works', async () => {
    const since = Date.now()
    await board.services.call('SYS', 'STOP')
    expect(await board.console.waitFor(SAFE, since, 2000)).toBeDefined()
    expect(await suspended(4)).toBe(1)
    await leaveSafeState(board.link)
    expect(await suspended(4)).toBe(0)
  })
})

describe('a link that is lost', () => {
  it('with on_link_lost the board enters the safe state at once, and the app infers it after the reconnect', async () => {
    const deadman = new DeadMan(board.link, { timeoutMs: 5000 })
    const seen = events(deadman)
    await deadman.start()
    const since = Date.now()
    await board.platform.dropFromHost()
    // Windows keeps the GATT session of a closed client until its supervision timeout: the board hears of the loss after seconds
    const logged = await board.console.waitFor(SAFE, since, 10_000)
    expect(logged, 'the board logged its safe state').toBeDefined()
    expect(await board.console.waitFor(/safe state \(reason 3\)/, since, 1000), 'because the link was lost, not for silence').toBeDefined()
    const lapsed = await until(() => seen.find((e) => e.type === 'lapsed'), 12_000, 100)
    expect(lapsed).toMatchObject({ type: 'lapsed', reason: 'link-lost' })
    expect(seen.some((e) => e.type === 'restarted')).toBe(false) // the board did not restart: it only stopped
    expect(await suspended(4)).toBe(1)
    await deadman.stop()
    await leaveSafeState(board.link)
    expect(await suspended(4)).toBe(0)
  })

  it('without on_link_lost a drop that is quicker than the timeout is no safe state', async () => {
    const deadman = new DeadMan(board.link, { timeoutMs: 8000, onLinkLost: false })
    const seen = events(deadman)
    await deadman.start()
    const since = Date.now()
    await board.platform.dropFromHost()
    await board.services.call('SYS', 'PING') // waits for the link, answers
    await sleep(500)
    expect(board.console.since(since, SAFE)).toEqual([])
    expect(seen.filter((e) => e.type === 'lapsed')).toEqual([])
    await deadman.stop()
  })
})

describe('a board restart under the dead-man', () => {
  it('the app sees "restarted": the dead-man is armed again after it, the program and devices are as at boot', async () => {
    const deadman = new DeadMan(board.link, { timeoutMs: 3000 })
    const seen = events(deadman)
    await deadman.start()
    await board.services.call('SYS', 'RESTART')
    expect(await until(() => seen.find((e) => e.type === 'restarted'), 25_000, 200)).toBeDefined()
    expect(await until(async () => (await getDeadman()).timeout_ms === 3000, 8000, 200)).toBe(true) // armed again, the RAM setting was lost
    await deadman.stop()
    expect((await getDeadman()).timeout_ms).toBe(0)
  })

  it('the console logged no panic', () => {
    expect(board.console.trouble(board.startedAt)).toEqual([])
  })
})
