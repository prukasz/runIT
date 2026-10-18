import { bundledLinkDefinitions } from '@backend'
import type { BleChannel, BleDeviceRef, BlePlatform, OpenHandlers } from '@backend'
import { fragment, Reassembler } from '@backend'

export const defs = bundledLinkDefinitions()

/** A board on the far side of a fake platform: it reassembles what is written, answers with whatever the test says. */
export class FakeBoard implements BlePlatform {
  readonly available = true
  readonly device: BleDeviceRef = { id: 'board-1', name: 'runit' }
  /** Complete request frames the board received, in order. */
  readonly requests: Uint8Array[] = []
  /** Packet lengths seen on the wire, for the framing assertions. */
  readonly packets: number[] = []
  opens = 0
  closes = 0
  /** Opens that fail before one works. */
  failOpens = 0
  /** Opens that never finish (until `release`). */
  hangOpens = 0
  private readonly late: (() => void)[] = []
  /** Called for each complete request; answer with `send` (a whole frame, fragmented for you). */
  respond: (frame: Uint8Array, send: (frame: Uint8Array, characteristic?: number) => void) => void = () => undefined
  private handlers: OpenHandlers | undefined
  private readonly reassembler = new Reassembler(defs)
  /** A write fails with this error once, when set. */
  failNextWrite: Error | undefined
  /** Writes wait for this before they finish (a slow link). */
  writeDelayMs = 0

  async requestDevice(): Promise<BleDeviceRef> {
    return this.device
  }

  async knownDevices(): Promise<readonly BleDeviceRef[]> {
    return [this.device]
  }

  async forget(): Promise<void> {}

  open(_device: BleDeviceRef, handlers: OpenHandlers): Promise<BleChannel> {
    this.opens++
    if (this.hangOpens > 0) {
      this.hangOpens--
      return new Promise((resolve) => this.late.push(() => resolve(this.channel(handlers))))
    }
    if (this.failOpens > 0) {
      this.failOpens--
      return Promise.reject(new Error('GATT connect failed'))
    }
    return Promise.resolve(this.channel(handlers))
  }

  /** Lets the opens that hung finish. */
  release(): void {
    this.late.splice(0).forEach((finish) => finish())
  }

  private channel(handlers: OpenHandlers): BleChannel {
    this.handlers = handlers
    this.reassembler.reset()
    return {
      write: async (packet) => {
        if (this.writeDelayMs) await new Promise((resolve) => setTimeout(resolve, this.writeDelayMs))
        if (this.failNextWrite) {
          const error = this.failNextWrite
          this.failNextWrite = undefined
          throw error
        }
        this.packets.push(packet.length)
        const result = this.reassembler.push(packet)
        if (result && 'frame' in result) {
          this.requests.push(result.frame)
          this.respond(result.frame, (frame, characteristic = defs.gatt.tx) => this.notify(frame, characteristic))
        }
      },
      close: async () => {
        this.closes++
        this.handlers = undefined
      },
    }
  }

  /** A frame from the board, as notifications of 20 bytes (the smallest link). */
  notify(frame: Uint8Array, characteristic: number = defs.gatt.tx): void {
    const handlers = this.handlers
    if (!handlers) return
    for (const packet of fragment(defs, frame, 20)) queueMicrotask(() => handlers.onPacket(characteristic, packet))
  }

  /** The link drops by itself. */
  drop(reason = 'out of range'): void {
    const handlers = this.handlers
    this.handlers = undefined
    handlers?.onClosed(reason)
  }
}

export const answer = (seq: number, data: number[] = [], ok = true): Uint8Array => Uint8Array.from([defs.streams.response, seq, ok ? defs.status.ok : defs.status.error, ...data])

export const until = async (condition: () => boolean, ms = 500): Promise<void> => {
  const start = Date.now()
  while (!condition()) {
    if (Date.now() - start > ms) throw new Error('Timed out waiting for the condition.')
    await new Promise((resolve) => setTimeout(resolve, 1))
  }
}
