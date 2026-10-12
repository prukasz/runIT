import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { PingMonitor } from '../../src/ble/ping'
import { RunitBleLink } from '../../src/ble/link'
import type { FrameStream } from '../../src/ble/link'

/** A link that is connected and records what is sent; frames are injected by the test. */
const fakeLink = () => {
  const frameHandlers = new Set<(s: FrameStream, f: Uint8Array) => void>()
  const stateHandlers = new Set<(s: string) => void>()
  const sent: Uint8Array[] = []
  const link = {
    state: 'connected',
    onFrame: (h: (s: FrameStream, f: Uint8Array) => void) => (frameHandlers.add(h), () => frameHandlers.delete(h)),
    onState: (h: (s: string) => void) => (stateHandlers.add(h), () => stateHandlers.delete(h)),
    send: async (f: Uint8Array) => void sent.push(f),
  } as unknown as RunitBleLink
  const answer = (seq: number, uptime: number, status = 0) => {
    const f = new Uint8Array(7)
    f[0] = 5
    f[1] = seq
    f[2] = status
    new DataView(f.buffer).setUint32(3, uptime, true)
    frameHandlers.forEach((h) => h('main', f))
  }
  return { link, sent, answer }
}

describe('PingMonitor', () => {
  beforeEach(() => vi.useFakeTimers())
  afterEach(() => vi.useRealTimers())

  it('sends [seq][0][SYS][PING] and reads the round trip and uptime from the answer', async () => {
    const { link, sent, answer } = fakeLink()
    const monitor = new PingMonitor(link, { intervalMs: 1000 })
    await vi.advanceTimersByTimeAsync(0)
    expect(Array.from(sent[0])).toEqual([200, 0, 1, 1])
    await vi.advanceTimersByTimeAsync(30)
    answer(200, 5000)
    expect(monitor.current).toMatchObject({ missed: 0, uptimeMs: 5000, rttMs: 30, restarted: false })
    monitor.close()
  })

  it('counts pings without an answer, and clears the count when the board answers again', async () => {
    const { link, sent, answer } = fakeLink()
    const monitor = new PingMonitor(link, { intervalMs: 1000, timeoutMs: 800 })
    await vi.advanceTimersByTimeAsync(3100) // pings 1..4, none answered
    expect(sent.length).toBe(4)
    expect(monitor.current.missed).toBe(3)
    answer(203, 9000)
    expect(monitor.current.missed).toBe(0)
    monitor.close()
  })

  it('a smaller uptime than before means the board restarted', async () => {
    const { link, answer } = fakeLink()
    const monitor = new PingMonitor(link, { intervalMs: 1000 })
    await vi.advanceTimersByTimeAsync(0)
    answer(200, 20000)
    await vi.advanceTimersByTimeAsync(1000)
    answer(201, 700)
    expect(monitor.current.restarted).toBe(true)
    monitor.close()
  })

  it('ignores answers to nothing it asked, errors and other frames', async () => {
    const { link, answer } = fakeLink()
    const monitor = new PingMonitor(link, { intervalMs: 1000 })
    await vi.advanceTimersByTimeAsync(0)
    answer(77, 1)
    answer(200, 1, 1)
    expect(monitor.current.uptimeMs).toBeUndefined()
    monitor.close()
  })
})
