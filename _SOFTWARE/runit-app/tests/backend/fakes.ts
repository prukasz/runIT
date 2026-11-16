import { bundledLinkDefinitions, bundledServiceCatalog, decodeFrame, decodeHello, encodeFrame, HELLO_BYTES, serviceCall } from '@backend'
import type { Hello } from '@backend'
import type { BleChannel, BleDeviceRef, BlePlatform, OpenHandlers } from '@backend'
import { fragment, Reassembler } from '@backend'

export const defs = bundledLinkDefinitions()
const peers = serviceCall(bundledServiceCatalog(), 'SESSION', 'PEERS').request

/** The app's id (the first client id: the link says it in its hello), and the fake board's own. */
export const CLIENT_ID = defs.session.id.app
export const BOARD_ID = 0x00

/**
 * A board on the far side of a fake platform: it reassembles what is written, opens the session itself (HELLO, SESSION PEERS) and
 * answers the other requests with whatever the test says. A test sees a request as `[seq low byte][flags][key][dir]...`: the
 * session header folded into two bytes, so `frame[0]` is the seq to answer and `frame[2]` the root key.
 */
export class FakeBoard implements BlePlatform {
  readonly available = true
  readonly device: BleDeviceRef = { id: 'board-1', name: 'runit' }
  /** The board's id, in its hello. */
  boardId = BOARD_ID
  /** The client's last hello, decoded. */
  clientHello: Hello | undefined
  /** The dead-man's time of the client's session, as SESSION PEERS tells it (answered by the board itself, not shown to the test). */
  deadmanMs = 0
  /** Complete request frames the board received, in order, as a test sees them. */
  readonly requests: Uint8Array[] = []
  /** Packet lengths of those requests on the wire, for the framing assertions. */
  readonly packets: number[] = []
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
  private readonly late: (() => void)[] = []
  /** Called for each complete request; answer with `send` (a whole frame, fragmented for you). */
  respond: (frame: Uint8Array, send: (frame: Uint8Array, characteristic?: number) => void) => void = () => undefined
  private handlers: OpenHandlers | undefined
  private readonly reassembler = new Reassembler(defs)
  private framePackets: number[] = []
  /** The full seq of the last request by its low byte: what `answer(seq)` of a test answers. */
  private readonly seqs = new Map<number, number>()
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
    if (frame.kind === defs.session.kind.control) {
      if (frame.body[0] === defs.session.control.hello) {
        this.clientHello = decodeHello(defs, frame.body)
        this.notify(this.hello())
      }
      else if (frame.body[0] === defs.session.control.ping) this.pings++
      return
    }
    if (frame.kind !== defs.session.kind.request) return
    if (frame.body.length === peers.keys.length && peers.keys.every((k, i) => frame.body[i] === k)) {
      this.notify(encodeFrame(defs, { kind: defs.session.kind.answer, seq: frame.seq }, this.peers()))
      return
    }
    this.packets.push(...packets)
    this.seqs.set(frame.seq & 0xff, frame.seq)
    const view = new Uint8Array(2 + frame.body.length)
    view[0] = frame.seq & 0xff
    view[1] = frame.flags
    view.set(frame.body, 2)
    this.requests.push(view)
    this.respond(view, (out, characteristic = defs.gatt.tx) => this.notify(this.fullSeq(out), characteristic))
  }

  /** The board's HELLO (sys_session_hello_t): its own id (`boardId`), its boot id, its build. */
  hello(): Uint8Array {
    const body = new Uint8Array(1 + HELLO_BYTES)
    const view = new DataView(body.buffer)
    body[0] = defs.session.control.hello
    body[1] = this.boardId
    view.setUint32(2, this.bootId, true)
    view.setUint32(6, 0xb17d1d, true)
    return encodeFrame(defs, { kind: defs.session.kind.control, seq: 0 }, body)
  }

  /** SESSION PEERS: the one session, the client's (sys_session_info_t, 10 B). */
  private peers(): Uint8Array {
    const body = new Uint8Array(1 + 10)
    const view = new DataView(body.buffer)
    body[0] = 1
    body[1] = 1 // provider
    body[2] = 1 // open
    body[3] = this.clientHello?.id ?? CLIENT_ID
    body[4] = 0 // safety
    view.setUint16(1 + 8, this.deadmanMs, true)
    return body
  }

  /** An answer a test made with `answer(seq low byte)` gets the full seq of that request. */
  private fullSeq(frame: Uint8Array): Uint8Array {
    const f = decodeFrame(defs, frame)
    if (!f || f.kind !== defs.session.kind.answer) return frame
    const seq = this.seqs.get(f.seq & 0xff) ?? f.seq
    const out = frame.slice()
    out[1] = seq & 0xff
    out[2] = seq >> 8
    return out
  }

  /** A frame from the board, as notifications of 20 bytes (the smallest link). */
  notify(frame: Uint8Array, characteristic: number = defs.gatt.tx): void {
    const handlers = this.handlers
    if (!handlers) return
    for (const packet of fragment(defs, frame, 20)) queueMicrotask(() => handlers.onPacket(characteristic, packet))
  }

  /** A PUSH frame as the board sends it: a stream's or a session channel's (from 0x80). */
  stream(id: number, payload: number[] | Uint8Array, seq = 0): void {
    this.notify(encodeFrame(defs, { kind: defs.session.kind.push, seq }, Uint8Array.from([id, ...payload])))
  }

  /** The link drops by itself. */
  drop(reason = 'out of range'): void {
    const handlers = this.handlers
    this.handlers = undefined
    handlers?.onClosed(reason)
  }
}

/** An answer to the request whose seq (low byte) is `seq`: OK with `data`, or ERROR with `data` as the error record. */
export const answer = (seq: number, data: number[] = [], ok = true): Uint8Array =>
  encodeFrame(defs, { kind: defs.session.kind.answer, flags: 0, seq }, Uint8Array.from(data)).map((b, i) =>
    i === 0 && !ok ? b | defs.session.frame.status : b,
  )

export const until = async (condition: () => boolean, ms = 500): Promise<void> => {
  const start = Date.now()
  while (!condition()) {
    if (Date.now() - start > ms) throw new Error('Timed out waiting for the condition.')
    await new Promise((resolve) => setTimeout(resolve, 1))
  }
}
