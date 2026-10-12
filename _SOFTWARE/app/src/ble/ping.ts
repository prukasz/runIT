import { routerKey, routerOp } from '../backend/catalog'
import { STATUS_OK, STREAM } from '../backend/protocol/frames'
import type { RunitBleLink } from './link'

/** The sys router's PING answers `{ u32 uptime_ms }`. Responses are `[response stream][seq][status][data]`. */
const KEY_SYS = routerKey('ROUTER_KEY_SYS')
const OP_PING = routerOp('ROUTER_KEY_SYS', 'OP_PING')
const RESPONSE = STREAM.response

export interface PingStatus {
  /** Pings sent since the link came up. */
  readonly sent: number
  /** Answers missing in a row (0 while the board answers). */
  readonly missed: number
  /** Round trip of the last answer, ms. */
  readonly rttMs?: number
  /** The board's uptime in its last answer, ms: a smaller value than before means the board restarted. */
  readonly uptimeMs?: number
  /** The uptime went backwards since the last answer: the board rebooted while the link looked fine. */
  readonly restarted: boolean
}

/**
 * Checks that the board answers, not only that the radio link is up: a ping every `intervalMs` while connected.
 * `missedLimit` unanswered pings in a row mean the board is not answering (stuck, or the link is half dead).
 */
export class PingMonitor {
  private timer?: ReturnType<typeof setInterval>
  /** The ping's seq numbers are 200..254; the RouterClient's are 1..199 (SEQ_LAST), tools may use 255. */
  private seq = 199
  private pending = new Map<number, number>()
  private status: PingStatus = { sent: 0, missed: 0, restarted: false }
  private readonly handlers = new Set<(status: PingStatus) => void>()
  private readonly stops: (() => void)[] = []

  private readonly link: RunitBleLink
  private readonly options: { readonly intervalMs?: number; readonly timeoutMs?: number; readonly now?: () => number }

  constructor(link: RunitBleLink, options: { readonly intervalMs?: number; readonly timeoutMs?: number; readonly now?: () => number } = {}) {
    this.link = link
    this.options = options
    this.stops.push(link.onFrame((stream, frame) => stream === 'main' && this.answer(frame)))
    this.stops.push(link.onState((state) => (state === 'connected' ? this.start() : this.stop())))
    if (link.state === 'connected') this.start()
  }

  get current(): PingStatus {
    return this.status
  }

  onStatus(handler: (status: PingStatus) => void): () => void {
    this.handlers.add(handler)
    return () => this.handlers.delete(handler)
  }

  close(): void {
    this.stop()
    this.stops.forEach((stop) => stop())
  }

  private now(): number {
    return (this.options.now ?? Date.now)()
  }

  private start(): void {
    this.stop()
    this.status = { sent: 0, missed: 0, restarted: false }
    this.emit()
    void this.ping()
    this.timer = setInterval(() => void this.ping(), this.options.intervalMs ?? 2000)
  }

  private stop(): void {
    if (this.timer) clearInterval(this.timer)
    this.timer = undefined
    this.pending.clear()
  }

  private async ping(): Promise<void> {
    const timeout = this.options.timeoutMs ?? 1500
    // Pings that got no answer in time count as missed.
    const t = this.now()
    let late = 0
    for (const [seq, sentAt] of this.pending) {
      if (t - sentAt >= timeout) {
        this.pending.delete(seq)
        late++
      }
    }
    if (late) this.status = { ...this.status, missed: this.status.missed + late }
    this.seq = this.seq >= 254 ? 200 : this.seq + 1
    this.pending.set(this.seq, t)
    this.status = { ...this.status, sent: this.status.sent + 1 }
    this.emit()
    try {
      await this.link.send(Uint8Array.of(this.seq, 0, KEY_SYS, OP_PING))
    } catch {
      this.pending.delete(this.seq)
    }
  }

  private answer(frame: Uint8Array): void {
    if (frame.length < 3 || frame[0] !== RESPONSE) return
    const sentAt = this.pending.get(frame[1])
    if (sentAt === undefined) return
    this.pending.delete(frame[1])
    if (frame[2] !== STATUS_OK || frame.length < 7) return
    const uptime = new DataView(frame.buffer, frame.byteOffset + 3, 4).getUint32(0, true)
    const restarted = this.status.uptimeMs !== undefined && uptime < this.status.uptimeMs
    this.status = { ...this.status, missed: 0, rttMs: this.now() - sentAt, uptimeMs: uptime, restarted }
    this.emit()
  }

  private emit(): void {
    this.handlers.forEach((handler) => handler(this.status))
  }
}
