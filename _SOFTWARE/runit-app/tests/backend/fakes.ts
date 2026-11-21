import { bundledLinkDefinitions, bundledServiceCatalog, decodeFrame, decodeHello, encodeFrame, fragment, HELLO_BYTES, peersCall, Reassembler } from '@backend'
import type { BleChannel, BleDeviceRef, BlePlatform, Frame, Hello, OpenHandlers, OpenSpec } from '@backend'

export const defs = bundledLinkDefinitions()

/** The app's id (the first client id: the link says it as the source of its frames), and the fake board's own. */
export const CLIENT_ID = defs.src.clientFirst
export const BOARD_ID = 0x01

/** SESSION PEERS (the board answers it itself) and its row (sys_session_info_t, 10 B). */
const PEERS = peersCall(bundledServiceCatalog()).request
const PEER_ROW = 10

/**
 * A board on the far side of a fake platform: it reassembles what is written on the session characteristic, opens the session
 * itself (HELLO, SESSION PEERS) and hands every other request to `respond` as a decoded frame (`r.key`, `r.seq`, `r.flags`,
 * `r.data` = `[dir][...]`); a test answers with `send(answer(r, data))`.
 */
export class FakeBoard implements BlePlatform {
  readonly available = true
  readonly device: BleDeviceRef = { id: 'board-1', name: 'runit' }
  /** The board's id, the source of its frames. */
  boardId = BOARD_ID
  /** The client's last hello. */
  clientHello: Hello | undefined
  /** The dead-man's time of the client's session, as SESSION PEERS tells it. */
  deadmanMs = 0
  /** The requests the board received (but its session's own), in order. */
  readonly requests: Frame[] = []
  /** Packet lengths of those requests on the wire. */
  readonly packets: number[] = []
  /** What the link asked to open. */
  spec: OpenSpec | undefined
  opens = 0
  closes = 0
  /** Opens that fail before one works. */
  failOpens = 0
  /** Opens that never finish (until `release`). */
  hangOpens = 0
  /** The board's boot id: a new one in a HELLO is a restart. */
  bootId = 1
  /** PINGs received. */
  pings = 0
  /** A write fails with this error once, when set. */
  failNextWrite: Error | undefined
  /** Writes wait this long before they finish (a slow link). */
  writeDelayMs = 0
  /** Called for each request; answer with `send` (a whole frame, fragmented for you). */
  respond: (request: Frame, send: (frame: Uint8Array) => void) => void = () => undefined
  private readonly late: (() => void)[] = []
  private handlers: OpenHandlers | undefined
  private readonly reassembler = new Reassembler(defs)
  private framePackets: number[] = []

  async requestDevice(): Promise<BleDeviceRef> {
    return this.device
  }

  async knownDevices(): Promise<readonly BleDeviceRef[]> {
    return [this.device]
  }

  async forget(): Promise<void> {}

  open(_device: BleDeviceRef, spec: OpenSpec, handlers: OpenHandlers): Promise<BleChannel> {
    this.opens++
    this.spec = spec
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
    this.framePackets = []
    return {
      write: async (packet) => {
        if (this.writeDelayMs) await new Promise((resolve) => setTimeout(resolve, this.writeDelayMs))
        if (this.failNextWrite) {
          const error = this.failNextWrite
          this.failNextWrite = undefined
          throw error
        }
        this.framePackets.push(packet.length)
        const result = this.reassembler.push(packet)
        if (result && 'frame' in result) {
          const packets = this.framePackets
          this.framePackets = []
          this.received(result.frame, packets)
        } else if (result) this.framePackets = []
      },
      close: async () => {
        this.closes++
        this.handlers = undefined
      },
    }
  }

  private received(raw: Uint8Array, packets: number[]): void {
    const frame = decodeFrame(defs, raw)
    if (!frame) return
    if (frame.kind === defs.kind.control) {
      const hello = decodeHello(defs, frame)
      if (hello) {
        this.clientHello = hello
        this.notify(this.hello())
      } else if (frame.data[0] === defs.control.ping) this.pings++
      return
    }
    if (frame.kind !== defs.kind.request) return
    if (frame.key === PEERS.key && frame.data.length === PEERS.body.length && frame.data.every((b, i) => b === PEERS.body[i])) {
      this.notify(answer(frame, [...this.peers()]))
      return
    }
    this.packets.push(...packets)
    this.requests.push(frame)
    this.respond(frame, (out) => this.notify(out))
  }

  /** The board's HELLO: its id as the source, its boot id, its build. */
  hello(bootId = this.bootId): Uint8Array {
    const data = new Uint8Array(1 + HELLO_BYTES)
    const view = new DataView(data.buffer)
    data[0] = defs.control.hello
    view.setUint32(1, bootId, true)
    view.setUint32(5, 0xb17d1d, true)
    return encodeFrame(defs, { kind: defs.kind.control, src: this.boardId, key: defs.control.key, seq: 0 }, data)
  }

  /** A control frame of the board: `[op][data]`. */
  control(op: number): Uint8Array {
    return encodeFrame(defs, { kind: defs.kind.control, src: this.boardId, key: defs.control.key, seq: 0 }, Uint8Array.of(op))
  }

  /** SESSION PEERS: a count, then the client's session (provider, open, peer id, safety, build u32, dead-man u16). */
  private peers(): Uint8Array {
    const data = new Uint8Array(1 + PEER_ROW)
    const view = new DataView(data.buffer)
    data[0] = 1
    data[1] = 3 // provider
    data[2] = 1 // open
    data[3] = this.clientHello?.id ?? CLIENT_ID
    data[4] = 0 // safety
    view.setUint16(1 + 8, this.deadmanMs, true)
    return data
  }

  /** A frame from the board, on the session characteristic (fragmented into packets of 20 B: the smallest link) or, raw, on another. */
  notify(frame: Uint8Array, characteristic: number = defs.gatt.tx): void {
    const handlers = this.handlers
    if (!handlers) return
    const packets = characteristic === defs.gatt.tx ? fragment(defs, frame, 20) : [frame]
    for (const packet of packets) queueMicrotask(() => handlers.onPacket(characteristic, packet))
  }

  /** A PUSH of a stream (its key), on the session characteristic or a raw one. */
  push(key: number, data: number[] | Uint8Array, { seq = 0, urgent = false, characteristic = defs.gatt.tx as number, src = this.boardId } = {}): void {
    this.notify(encodeFrame(defs, { kind: defs.kind.push, flags: urgent ? defs.flags.urgent : 0, src, key, seq }, Uint8Array.from(data)), characteristic)
  }

  /** The link drops by itself. */
  drop(reason = 'out of range'): void {
    const handlers = this.handlers
    this.handlers = undefined
    handlers?.onClosed(reason)
  }
}

/** The answer to `request`: OK with `data`, or ERROR with `data` as the error record. */
export const answer = (request: Pick<Frame, 'key' | 'seq'>, data: number[] | Uint8Array = [], ok = true, src = BOARD_ID): Uint8Array =>
  encodeFrame(defs, { kind: defs.kind.answer, error: !ok, src, key: request.key, seq: request.seq }, Uint8Array.from(data))

export const until = async (condition: () => boolean, ms = 500): Promise<void> => {
  const start = Date.now()
  while (!condition()) {
    if (Date.now() - start > ms) throw new Error('Timed out waiting for the condition.')
    await new Promise((resolve) => setTimeout(resolve, 1))
  }
}
