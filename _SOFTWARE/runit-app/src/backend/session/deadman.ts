import type { FieldRecord } from '../catalog/codec'
import type { ServiceCatalog } from '../catalog/services'
import { bundledServiceCatalog } from '../definitions'
import { decodeAnswer, serviceCall } from '../protocol/services'
import type { LinkStatus, RunitLink } from './link'

/*
 * The board's dead-man (SYS_SYSTEM.MD "Dead-man"), kept from the app. Armed, `timeout_ms` is the timeout of the clients'
 * sessions, and the board enters its safe state (VM stopped, every device suspended: RUNIT.MD) when this client's session
 * ends: nothing came for `timeout_ms`, or the link dropped. Any frame feeds it (the link PINGs on its own), so:
 *
 *   - armed with the DEADMAN write when the link opens, and again after every reconnect: the setting lives in RAM, a restart forgets it;
 *   - a heartbeat (PING, urgent: it passes commands the board is slow to answer) only when nothing was written for `heartbeatMs`;
 *   - after a reconnect, PING's uptime says whether the board restarted (it is then as at boot) and, if not, whether the drop or
 *     the silence most likely put it in its safe state. The board sends nothing when it enters it: this is the app's inference.
 *
 * The commands and fields are the descriptor's (`contract_sys.json`); `DEADMAN_COMMANDS` names them, the catalog lays them out.
 */

/** The dead-man's commands and fields in the sys service's descriptor (SYS_SYSTEM.MD "Remote"). */
export const DEADMAN_COMMANDS = {
  service: 'SYS',
  set: 'DEADMAN',
  ping: 'PING',
  timeout: 'timeout_ms',
  uptime: 'uptime_ms',
} as const

export interface DeadManOptions {
  /** Without a frame for this long, or when the link drops, the board enters its safe state (`0` turns the dead-man off; else 150..60000). */
  readonly timeoutMs: number
  /** A heartbeat when nothing was written for this long (default a third of the timeout). */
  readonly heartbeatMs?: number
  readonly catalog?: ServiceCatalog
}

export type DeadManEvent =
  | { readonly type: 'armed'; readonly timeoutMs: number }
  /** The board restarted while the link was down: its program, the devices a project made and the dead-man are as at boot. */
  | { readonly type: 'restarted'; readonly uptimeMs: number }
  /** The board most likely entered its safe state: the link dropped, or nothing reached it for the timeout. */
  | { readonly type: 'lapsed'; readonly reason: 'link-lost' | 'silence'; readonly silentMs: number }
  | { readonly type: 'failed'; readonly step: 'arm' | 'heartbeat' | 'disarm'; readonly error: unknown }

export class DeadMan {
  readonly timeoutMs: number
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
        await this.set(0)
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

  private async set(timeoutMs: number): Promise<void> {
    const call = serviceCall(this.catalog, DEADMAN_COMMANDS.service, DEADMAN_COMMANDS.set, { [DEADMAN_COMMANDS.timeout]: timeoutMs }, 'write')
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

  /** The DEADMAN write, then PING: what the board's uptime says about the time the link was down. */
  private async arm(): Promise<void> {
    const before = this.uptime
    const dropped = this.dropped
    this.dropped = undefined
    await this.set(this.timeoutMs)
    this.emit({ type: 'armed', timeoutMs: this.timeoutMs })
    const uptime = await this.ping()
    if (!before || !dropped) return
    if (uptime < before.ms) return this.emit({ type: 'restarted', uptimeMs: uptime })
    if (this.timeoutMs === 0) return
    const silentMs = Date.now() - dropped.lastSentAt
    this.emit({ type: 'lapsed', reason: silentMs >= this.timeoutMs ? 'silence' : 'link-lost', silentMs })
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
