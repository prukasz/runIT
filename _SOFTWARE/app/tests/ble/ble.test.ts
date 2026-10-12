import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { Reassembler, splitFrame } from '../../src/ble/framing'
import { RUNIT_LOGS, RUNIT_RX, RUNIT_SERVICE, RUNIT_TX, RunitBleLink } from '../../src/ble/link'

const bytes = (...values: number[]) => Uint8Array.from(values)
const frameOf = (n: number) => Uint8Array.from({ length: n }, (_, i) => i & 0xff)

describe('fragment framing (SYS_BLE.MD)', () => {
  it('a frame that fits one packet is C0 <frame>', () => {
    expect(splitFrame(bytes(1, 2, 3), 20)).toEqual([bytes(0xc0, 1, 2, 3)])
  })

  it('a three-packet frame is 40 .. 01 .. 82', () => {
    const packets = splitFrame(frameOf(10), 5)
    expect(packets.map((p) => p[0])).toEqual([0x40, 0x01, 0x82])
    expect(packets.map((p) => p.length)).toEqual([5, 5, 3])
  })

  it('round-trips every length up to the frame limit at several packet sizes', () => {
    for (const size of [2, 20, 185, 512]) {
      for (const n of [0, 1, size - 2, size - 1, size, 3 * size, 512]) {
        if (n > 512) continue
        const joiner = new Reassembler()
        const out = splitFrame(frameOf(n), size).map((p) => joiner.push(p)).filter((f) => f !== undefined)
        expect(out).toHaveLength(1)
        expect(out[0]).toEqual(frameOf(n))
      }
    }
  })

  it('the counter wraps at 64', () => {
    const packets = splitFrame(frameOf(200), 2)
    expect(packets[63][0] & 0x3f).toBe(63)
    expect(packets[64][0] & 0x3f).toBe(0)
    const joiner = new Reassembler()
    expect(packets.map((p) => joiner.push(p)).filter(Boolean)[0]).toEqual(frameOf(200))
  })

  it('refuses a frame over 512 bytes', () => {
    expect(() => splitFrame(frameOf(513), 185)).toThrow(RangeError)
  })

  it('a wrong counter or a packet outside a frame drops the frame up to the next FIRST', () => {
    const problems: string[] = []
    const joiner = new Reassembler((p) => problems.push(p))
    expect(joiner.push(bytes(0x81, 1))).toBeUndefined() // not inside a frame
    expect(joiner.push(bytes(0x40, 1))).toBeUndefined()
    expect(joiner.push(bytes(0x82, 2))).toBeUndefined() // skipped 1
    expect(joiner.push(bytes(0x81, 3))).toBeUndefined() // still dropped
    expect(joiner.push(bytes(0xc0, 9))).toEqual(bytes(9)) // next FIRST recovers
    expect(problems).toEqual(['out-of-frame', 'bad-counter', 'out-of-frame'])
  })
})

class FakeCharacteristic extends EventTarget {
  value?: DataView
  written: Uint8Array[] = []
  readonly uuid: string
  constructor(uuid: string) {
    super()
    this.uuid = uuid
  }
  writeValueWithResponse = vi.fn(async (v: Uint8Array) => { this.written.push(Uint8Array.from(v)) })
  writeValueWithoutResponse = vi.fn(async () => undefined)
  startNotifications = vi.fn(async () => this)
  stopNotifications = vi.fn(async () => this)
  notify(packet: Uint8Array) {
    this.value = new DataView(packet.buffer.slice(0))
    this.dispatchEvent(new Event('characteristicvaluechanged'))
  }
}

class FakeDevice extends EventTarget {
  connected = false
  reachable = true
  chars = new Map<number, FakeCharacteristic>([
    [RUNIT_TX, new FakeCharacteristic('tx')],
    [RUNIT_RX, new FakeCharacteristic('rx')],
    [RUNIT_LOGS, new FakeCharacteristic('logs')],
  ])
  connects = 0
  gatt = {
    get connected() { return device.connected },
    connect: async () => {
      if (!device.reachable) throw new Error('out of range')
      device.connected = true
      device.connects++
      return device.gatt
    },
    disconnect: () => { device.connected = false },
    getPrimaryService: async (uuid: number) => {
      if (uuid !== RUNIT_SERVICE) throw new Error('no service')
      return { uuid: 'svc', getCharacteristic: async (u: number) => device.chars.get(u)! }
    },
  }
  readonly id: string
  readonly name = 'runit'
  constructor(id: string) {
    super()
    this.id = id
  }
  /** The board goes away (power off, out of range). */
  drop() {
    this.connected = false
    this.reachable = false
    this.dispatchEvent(new Event('gattserverdisconnected'))
  }
}
let device: FakeDevice

const memory = () => {
  const map = new Map<string, string>()
  return { getItem: (k: string) => map.get(k) ?? null, setItem: (k: string, v: string) => void map.set(k, v), removeItem: (k: string) => void map.delete(k) }
}

const install = (known: FakeDevice[], picked?: FakeDevice) => {
  vi.stubGlobal('navigator', {
    bluetooth: {
      requestDevice: async () => {
        if (!picked) throw Object.assign(new Error('cancelled'), { name: 'NotFoundError' })
        return picked
      },
      getDevices: async () => known,
    },
  })
}

describe('RunitBleLink', () => {
  beforeEach(() => {
    device = new FakeDevice('board-1')
    vi.useFakeTimers()
  })
  afterEach(() => {
    vi.useRealTimers()
    vi.unstubAllGlobals()
  })

  it('pick connects, remembers the board, and passes frames both ways', async () => {
    install([], device)
    const storage = memory()
    const link = new RunitBleLink({ storage })
    const frames: [string, Uint8Array][] = []
    link.onFrame((s, f) => frames.push([s, f]))
    expect(await link.pick()).toBe(true)
    expect(link.state).toBe('connected')
    expect(storage.getItem('runit.ble.device')).toBe('board-1')

    device.chars.get(RUNIT_TX)!.notify(bytes(0xc0, 5, 6))
    device.chars.get(RUNIT_LOGS)!.notify(bytes(0x40, 1))
    device.chars.get(RUNIT_LOGS)!.notify(bytes(0x81, 2))
    expect(frames).toEqual([['main', bytes(5, 6)], ['logs', bytes(1, 2)]])

    await link.send(frameOf(400))
    const rx = device.chars.get(RUNIT_RX)!
    expect(rx.written.length).toBe(3)
    const joiner = new Reassembler()
    expect(rx.written.map((p) => joiner.push(p)).find(Boolean)).toEqual(frameOf(400))
  })

  it('a cancelled chooser is not an error', async () => {
    install([])
    expect(await new RunitBleLink({ storage: memory() }).pick()).toBe(false)
  })

  it('restore connects without a chooser, and says false when nothing is remembered', async () => {
    install([device])
    const storage = memory()
    expect(await new RunitBleLink({ storage }).restore()).toBe(true) // the only runit board
    install([])
    expect(await new RunitBleLink({ storage: memory() }).restore()).toBe(false)
  })

  it('restore after disconnect reuses the board of this session, even when the browser lists no permitted devices', async () => {
    install([], device) // getDevices() returns nothing
    const link = new RunitBleLink({ storage: memory() })
    await link.pick()
    await link.disconnect()
    expect(link.state).toBe('idle')
    expect(await link.restore()).toBe(true)
    expect(link.state).toBe('connected')
  })

  it('restore says why it cannot, when there is nothing to restore', async () => {
    install([])
    const link = new RunitBleLink({ storage: memory() })
    expect(await link.restore()).toBe(false)
    expect(link.events.at(-1)?.text).toContain('cannot restore')
  })

  it('reconnects by itself after a drop, with backoff, when advertisements cannot be watched', async () => {
    install([], device)
    const link = new RunitBleLink({ storage: memory(), retryMs: [100, 400] })
    const states: string[] = []
    link.onState((s) => states.push(s))
    await link.pick()
    device.drop()
    expect(link.state).toBe('waiting')
    await vi.advanceTimersByTimeAsync(100) // first attempt fails: out of range
    await vi.advanceTimersByTimeAsync(200) // second fails
    expect(link.state).toBe('waiting')
    device.reachable = true
    await vi.advanceTimersByTimeAsync(400)
    expect(link.state).toBe('connected')
    // the new link carries frames again
    const frames: Uint8Array[] = []
    link.onFrame((_, f) => frames.push(f))
    device.chars.get(RUNIT_TX)!.notify(bytes(0xc0, 7))
    expect(frames).toEqual([bytes(7)])
    expect(states).toContain('waiting')
  })

  it('reconnects as soon as the board advertises, when the browser can watch', async () => {
    const watch = vi.fn(async () => undefined)
    Object.assign(device, { watchAdvertisements: watch })
    install([], device)
    const link = new RunitBleLink({ storage: memory(), retryMs: [100, 400] })
    await link.pick()
    device.drop()
    await vi.advanceTimersByTimeAsync(0)
    expect(watch).toHaveBeenCalled()
    expect(link.state).toBe('waiting')
    device.reachable = true
    device.dispatchEvent(new Event('advertisementreceived'))
    await vi.advanceTimersByTimeAsync(0)
    expect(link.state).toBe('connected')
  })

  it('disconnect() ends the link and the reconnect; the board is not chased after it', async () => {
    install([], device)
    const link = new RunitBleLink({ storage: memory(), retryMs: [100, 400] })
    await link.pick()
    await link.disconnect()
    expect(link.state).toBe('idle')
    const connects = device.connects
    device.reachable = true
    await vi.advanceTimersByTimeAsync(5000)
    expect(device.connects).toBe(connects)
  })

  it('without auto-reconnect a drop leaves the link idle', async () => {
    install([], device)
    const link = new RunitBleLink({ storage: memory(), autoReconnect: false })
    await link.pick()
    device.drop()
    expect(link.state).toBe('idle')
  })

  it('send before connecting is refused', async () => {
    install([])
    await expect(new RunitBleLink({ storage: memory() }).send(bytes(1))).rejects.toThrow('not connected')
  })
})
