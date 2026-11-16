import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { CommandError, DeadMan, decodePeers } from '@backend'
import type { DeadManEvent } from '@backend'
import { openBoard, sleep, until } from './harness'
import type { Board } from './harness'

/*
 * The dead-man and the safe state (SYS_SYSTEM.MD). The board sends nothing when it enters its safe state, so what the app infers
 * is held against what the board says after: its devices suspended and SYS SAFE (reason, how long). Armed, the dead-man is the end
 * of this client's session: a silence the session's PINGs never leave. Device 0 is the witness. (A link lost on the radio is not run here:
 * Windows reuses the closed GATT connection, so the board never sees the drop of `dropFromHost`.)
 */

const GPIO = 0
let board: Board
beforeAll(async () => {
  board = await openBoard()
})
afterAll(async () => {
  await board.services.call('SYS', 'DEADMAN', { timeout_ms: 0 }, { side: 'write' }).catch(() => undefined)
  await board.services.call('SYS', 'SAFE_LEAVE').catch(() => undefined)
  await board.close()
})

const setDeadman = (timeout_ms: number) => board.services.call('SYS', 'DEADMAN', { timeout_ms }, { side: 'write' })
const getDeadman = async () => (await board.services.call('SYS', 'DEADMAN', undefined, { side: 'read' })).value as { timeout_ms: number }
const suspended = async () => ((await board.services.device(GPIO, 'DEVICE', 'INFO')).value as { suspended: boolean }).suspended
const safeReason = async () => (await board.services.call('SYS', 'SAFE', undefined, { side: 'read' }).then((a) => a.value as { reason: number }, () => undefined))?.reason
const mine = async () => decodePeers(board.link.defs, (await board.services.call('SESSION', 'PEERS')).data).find((p) => p.open && p.peerId === board.link.session!.clientId)
const events = (deadman: DeadMan) => {
  const seen: DeadManEvent[] = []
  deadman.onEvent((e) => seen.push(e))
  return seen
}

describe('the dead-man kept by the app', () => {
  it('arms the board for this session, feeds it past its timeout, and disarms it', async () => {
    const deadman = new DeadMan(board.link, { timeoutMs: 1500 })
    const seen = events(deadman)
    await deadman.start()
    expect(await getDeadman()).toEqual({ timeout_ms: 1500 })
    expect(await mine()).toMatchObject({ deadmanMs: 1500, safety: true })
    expect(seen[0]).toMatchObject({ type: 'armed', timeoutMs: 1500 })
    await sleep(4500) // three timeouts of silence from the caller: the heartbeat alone keeps the board out of the safe state
    expect(seen.filter((e) => e.type === 'lapsed' || e.type === 'failed')).toEqual([])
    expect(await suspended()).toBe(false)
    await deadman.stop()
    expect((await getDeadman()).timeout_ms).toBe(0)
    expect((await mine())?.deadmanMs).toBe(0)
  })

  it('a timeout the heartbeat cannot keep is refused', () => {
    expect(() => new DeadMan(board.link, { timeoutMs: 1500, heartbeatMs: 1500 }).start()).toThrow()
  })
})

describe('the board enters its safe state', () => {
  it('STOP puts it there at once: devices suspended, the board and the link stay up', async () => {
    await board.services.call('SYS', 'STOP')
    expect(await suspended()).toBe(true)
    expect(await safeReason()).toBeDefined()
    const error = await board.services.device(GPIO, 'IO', 'TOGGLE', { pin: 4 }).then(
      () => undefined,
      (e: unknown) => e,
    )
    expect(error).toBeInstanceOf(CommandError)
    expect((error as CommandError).record!.cause!.info?.name).toBe('ERR_DEV_SUSPENDED')
  })

  it('SAFE_LEAVE resumes every device', async () => {
    await board.services.call('SYS', 'SAFE_LEAVE')
    expect(await suspended()).toBe(false)
  })
})

describe('a session that goes silent', () => {
  it('armed and left alone, the board ends the session, enters the safe state, and the link opens the session again by itself', async () => {
    await setDeadman(600) // nothing feeds it: the link pings only for a dead-man it saw at open
    await sleep(2000)
    // the board says NO_SESSION to the first frame after its silence: that command is lost with the session, the link opens it again
    const lost = await board.services.call('SYS', 'PING').then(() => undefined, (e: unknown) => e)
    expect(lost).toBeInstanceOf(CommandError)
    expect(await until(() => board.link.state === 'connected', 6000, 50)).toBe(true)
    expect(await until(suspended, 6000, 100)).toBe(true)
    expect(await safeReason()).toBe(2) // SYS_SAFE_DEADMAN
    await board.services.call('SYS', 'SAFE_LEAVE')
    expect(await suspended()).toBe(false)
  })

  it('disarmed, a drop of the link is no safe state', async () => {
    await setDeadman(0).catch(() => undefined)
    await board.platform.dropFromHost()
    await board.services.call('SYS', 'PING') // waits for the link, answers
    await sleep(500)
    expect(await suspended()).toBe(false)
  })
})

describe('a board restart under the dead-man', () => {
  it('the app sees "restarted": the dead-man is armed again after it', async () => {
    const deadman = new DeadMan(board.link, { timeoutMs: 3000 })
    const seen = events(deadman)
    await deadman.start()
    await board.services.call('SYS', 'RESTART')
    expect(await until(() => seen.find((e) => e.type === 'restarted'), 25_000, 200)).toBeDefined()
    expect(await until(async () => (await getDeadman()).timeout_ms === 3000, 8000, 200)).toBe(true) // armed again, the RAM setting was lost
    await deadman.stop()
    expect((await getDeadman()).timeout_ms).toBe(0)
  })
})
