import type { FieldRecord } from '../catalog/codec'
import type { ServiceCatalog } from '../catalog/services'
import { bundledServiceCatalog } from '../definitions'
import { decodeAnswer, serviceCall } from '../protocol/services'
import type { LinkStatus, RunitLink } from './link'

/*
 * The board's dead-man (ROUTER.MD "Safe state and dead-man"), kept from the app. The board enters its safe state (VM stopped,
 * every device suspended: RUNIT.MD) when no frame came for `timeout_ms`, and at once when the link drops if `on_link_lost` is
 * set. Any frame feeds it, so:
 *
 *   - armed with DEADMAN_SET when the link opens, and again after every reconnect: the setting lives in RAM, a restart forgets it;
 *   - a heartbeat (PING, urgent: it passes commands the board is slow to answer) only when nothing was written for `heartbeatMs`;
 *   - after a reconnect, PING's uptime says whether the board restarted (it is then as at boot) and, if not, whether the drop or
 *     the silence most likely put it in its safe state. The board sends nothing when it enters it: this is the app's inference.
 *
 * The commands and fields are the descriptor's (`contract_sys.json`); `DEADMAN_COMMANDS` names them, the catalog lays them out.
 */

/** The dead-man's commands and fields in the sys service's descriptor (ROUTER.MD "sys router"). */
export const DEADMAN_COMMANDS = {
  service: 'SYS',
  set: 'DEADMAN_SET',
  ping: 'PING',
  timeout: 'timeout_ms',
  onLinkLost: 'on_link_lost',
  uptime: 'uptime_ms',
} as const

export interface DeadManOptions {
  /** Without a frame for this long the board enters its safe state (`0` turns the dead-man off). */
  readonly timeoutMs: number
  /** The board enters its safe state when the link drops (default true). */
  readonly onLinkLost?: boolean
  /** A heartbeat when nothing was written for this long (default a third of the timeout). */
  readonly heartbeatMs?: number
  readonly catalog?: ServiceCatalog
}

export type DeadManEvent =
  | { readonly type: 'armed'; readonly timeoutMs: number; readonly onLinkLost: boolean }
  /** The board restarted while the link was down: its program, the devices a project made and the dead-man are as at boot. */
  | { readonly type: 'restarted'; readonly uptimeMs: number }
  /** The board most likely entered its safe state: the link dropped (`on_link_lost`) or nothing reached it for the timeout. */
  | { readonly type: 'lapsed'; readonly reason: 'link-lost' | 'silence'; readonly silentMs: number }
  | { readonly type: 'failed'; readonly step: 'arm' | 'heartbeat' | 'disarm'; readonly error: unknown }

export class DeadMan {
  readonly timeoutMs: number
  readonly onLinkLost: boolean
  readonly heartbeatMs: number
  private readonly catalog: ServiceCatalog
  private readonly handlers = new Set<(event: DeadManEvent) => void>()
  private readonly unsubscribe: (() => void)[] = []
  private timer: ReturnType<typeof setInterval> | undefined
  private running = false
  private beating = false
  private lastSentAt = 0
  /** When the link left `connected`, and the last write before that. */
  private dropped: { readonly at: number; readonly lastSentAt: number } | undefined
  private uptime: { readonly ms: number; readonly at: number } | undefined
  private lapsedSilent = false

  constructor(
    private readonly link: RunitLink,
    options: DeadManOptions,
  ) {
    if (!Number.isInteger(options.timeoutMs) || options.timeoutMs < 0) throw new RangeError(`A dead-man timeout of ${options.timeoutMs} ms.`)
    this.timeoutMs = options.timeoutMs
    this.onLinkLost = options.onLinkLost ?? true
    this.heartbeatMs = Math.max(1, options.heartbeatMs ?? Math.floor(options.timeoutMs / 3))
    this.catalog = options.catalog ?? bundledServiceCatalog()
    if (this.timeoutMs > 0 && this.heartbeatMs * 1.5 >= this.timeoutMs) throw new RangeError(`A heartbeat every ${this.heartbeatMs} ms can miss a ${this.timeoutMs} ms dead-man (checked every half heartbeat).`)
  }

  get active(): boolean {
    return this.running
  }

  onEvent(handler: (event: DeadManEvent) => void): () => void {
    this.handlers.add(handler)
    return () => this.handlers.delete(handler)
  }

  /** Arms the board's dead-man now (when connected; else as soon as it is) and keeps it fed until `stop()`. */
  async start(): Promise<void> {
    if (this.running) return
    this.running = true
    this.lastSentAt = Date.now()
    this.unsubscribe.push(
      this.link.commands.observe((event) => {
        if (event.type === 'sent') this.lastSentAt = event.at
      }),
      this.link.onStatus((status) => this.onStatus(status)),
    )
    if (this.timeoutMs > 0) this.timer = setInterval(() => this.tick(), Math.max(1, Math.floor(this.heartbeatMs / 2)))
    if (this.link.state === 'connected') await this.arm()
  }

  /** Stops feeding; with `disarm` (default) first turns the board's dead-man off, else the board enters its safe state after the timeout. */
  async stop(options: { disarm?: boolean } = {}): Promise<void> {
    if (!this.running) return
    if ((options.disarm ?? true) && this.link.state === 'connected') {
      try {
        await this.set(0, false)
      } catch (error) {
        this.emit({ type: 'failed', step: 'disarm', error })
      }
    }
    this.running = false
    clearInterval(this.timer)
    this.timer = undefined
    this.unsubscribe.splice(0).forEach((off) => off())
  }

  /* ---- the commands ---- */

  private async set(timeoutMs: number, onLinkLost: boolean): Promise<void> {
    const call = serviceCall(this.catalog, DEADMAN_COMMANDS.service, DEADMAN_COMMANDS.set, { [DEADMAN_COMMANDS.timeout]: timeoutMs, [DEADMAN_COMMANDS.onLinkLost]: onLinkLost })
    await this.link.request({ ...call.request, urgent: true }, { label: call.label })
  }

  /** The board's uptime (ms). */
  private async ping(): Promise<number> {
    const call = serviceCall(this.catalog, DEADMAN_COMMANDS.service, DEADMAN_COMMANDS.ping)
    const value = decodeAnswer(call, await this.link.request({ ...call.request, urgent: true }, { label: call.label })) as FieldRecord
    const ms = Number(value[DEADMAN_COMMANDS.uptime])
    this.uptime = { ms, at: Date.now() }
    return ms
  }

  /** DEADMAN_SET, then PING: what the board's uptime says about the time the link was down. */
  private async arm(): Promise<void> {
    const before = this.uptime
    const dropped = this.dropped
    this.dropped = undefined
    await this.set(this.timeoutMs, this.onLinkLost)
    this.emit({ type: 'armed', timeoutMs: this.timeoutMs, onLinkLost: this.onLinkLost })
    const uptime = await this.ping()
    if (!before || !dropped) return
    if (uptime < before.ms) return this.emit({ type: 'restarted', uptimeMs: uptime })
    if (this.timeoutMs === 0) return
    const silentMs = Date.now() - dropped.lastSentAt
    if (this.onLinkLost) this.emit({ type: 'lapsed', reason: 'link-lost', silentMs })
    else if (silentMs >= this.timeoutMs) this.emit({ type: 'lapsed', reason: 'silence', silentMs })
  }

  private onStatus(status: LinkStatus): void {
    if (status.state === 'connected') {
      this.lapsedSilent = false
      void this.arm().catch((error: unknown) => this.emit({ type: 'failed', step: 'arm', error }))
    } else if (!this.dropped && (status.state === 'reconnecting' || status.state === 'idle')) {
      this.dropped = { at: Date.now(), lastSentAt: this.lastSentAt }
    }
  }

  /** A heartbeat when nothing was written for `heartbeatMs`; a silence as long as the timeout is reported once. */
  private tick(): void {
    if (this.link.state !== 'connected') return
    const idle = Date.now() - this.lastSentAt
    if (idle >= this.timeoutMs && !this.lapsedSilent) {
      this.lapsedSilent = true // a timer held back (a tab in the background, a busy event loop) let the board time out
      this.emit({ type: 'lapsed', reason: 'silence', silentMs: idle })
    }
    if (idle < this.heartbeatMs || this.beating) return
    this.beating = true
    this.ping()
      .then(() => (this.lapsedSilent = false))
      .catch((error: unknown) => this.emit({ type: 'failed', step: 'heartbeat', error }))
      .finally(() => (this.beating = false))
  }

  private emit(event: DeadManEvent): void {
    this.handlers.forEach((handler) => handler(event))
  }
}
