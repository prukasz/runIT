import { bundledLinkDefinitions, errorCatalog } from '../definitions'
import type { ErrorInfo, LinkDefinitions } from '../definitions'
import { decodeFrame, decodeHello, decodeResponse, encodeFrame, encodeHello } from '../protocol/frames'
import type { Request } from '../protocol/frames'
import { fragment, Reassembler } from '../transport/framing'
import type { BleChannel, BleDeviceRef, BlePlatform } from '../transport/types'
import { CommandClient, CommandError } from './commands'
import type { CommandOptions } from './commands'

/*
 * The link to one board: a session (SYS_SESSION.MD), the commands, the streams, and a connection that keeps itself up.
 *
 *   - Opening is one call of the platform (connect, runIT service, two subscriptions), then the session: this client's HELLO,
 *     the board's in answer with the id it gave this client, then SUBSCRIBE of the errors, telemetry and events streams. The log
 *     lines come as they are on the LOGS characteristic (the board config binds the logs stream to it).
 *   - The session lives on PINGs: something goes to the board at least every third of its timeout (its HELLO says it). A board
 *     that answers NO_SESSION (it restarted, or ended the session) gets a new HELLO and the streams are subscribed again.
 *   - A link that drops by itself is reopened at once (the first retry has no delay); the delays grow only when the board does
 *     not come back, to a ceiling, and never stop until `disconnect()`.
 *   - A command in flight when the link drops fails with `link-lost` (it may have run: only the caller knows whether it can be
 *     repeated). A command given while the link is coming back waits for it, up to `waitForLinkMs`.
 *   - A write that fails recycles the link at once, and so do commands that stop being answered (`stallAfter` timeouts in a row):
 *     nothing says the link is gone after a GATT change, when the OS invalidates the handles and reports no disconnect.
 *   - Frames of one request never interleave on the wire: the board reassembles with a single buffer.
 */

export type LinkState = 'idle' | 'connecting' | 'connected' | 'reconnecting' | 'closed'

export interface LinkStatus {
  readonly state: LinkState
  /** Why the link left `connected` or failed to open. */
  readonly reason?: string
  /** While reconnecting: the attempt (0 is the first). */
  readonly attempt?: number
}

export interface ReconnectPolicy {
  /** Wait before attempt n (the last repeats). The first is 0: a board that dropped is usually advertising again already. */
  readonly delaysMs: readonly number[]
  /** Give up after this many failed attempts in a row (default: never). */
  readonly maxAttempts: number
  /** One attempt that neither succeeds nor fails is dropped after this long. */
  readonly openTimeoutMs: number
}

export const DEFAULT_RECONNECT: ReconnectPolicy = { delaysMs: [0, 100, 250, 500, 1000, 2000], maxAttempts: Number.POSITIVE_INFINITY, openTimeoutMs: 10_000 }

export interface LinkOptions {
  readonly defs?: LinkDefinitions
  readonly errors?: ReadonlyMap<number, ErrorInfo>
  /**
   * Bytes of a packet written to the board, header included; at most 512, the longest value ATT allows (a longer write is refused
   * with Invalid Attribute Value Length). Default 512: a frame of up to 511 B is one write, a frame of 512 B two. The link's MTU is
   * 527 when the central takes what the board offers; below that an acknowledged write longer than the MTU is split by the
   * platform (a long write).
   */
  readonly packetBytes?: number
  /** Default wait for an answer (ms). */
  readonly timeoutMs?: number
  readonly maxInFlight?: number
  /** Timeouts in a row after which the link is recycled (default 3). */
  readonly stallAfter?: number
  /** `false`: a lost link stays lost. */
  readonly reconnect?: Partial<ReconnectPolicy> | false
  /** How long a command waits for a link that is coming back (default 5000 ms). */
  readonly waitForLinkMs?: number
}

/** The longest value of a GATT attribute (BLE Core spec): the largest packet one write can carry. */
export const ATT_VALUE_MAX = 512

export const STREAM_NAMES = ['telemetry', 'logs', 'errors', 'events', 'watches'] as const
export type StreamName = (typeof STREAM_NAMES)[number]

/** The streams the link subscribes to once the session opens (logs come on their own characteristic; the watches' entries come to the one who added the watch, with no subscription). */
const SUBSCRIBED: readonly StreamName[] = ['errors', 'telemetry', 'events']

/** How long the board waits for this client: the host never ends the session itself. */
const CLIENT_TIMEOUT_MS = 60_000
/** A HELLO that gets no answer is sent again this often while the session opens. */
const HELLO_RETRY_MS = 500
/** The connector ops of SUBSCRIBE (SYS_DATA_CONNECTOR.MD "Remote"). */
const CONN_SUBSCRIBE = 0x09

/** The session as the board's HELLO gave it. */
export interface SessionInfo {
  /** This client's id: the `src` of its requests. */
  readonly clientId: number
  readonly boardId: number
  readonly bootId: number
  /** The board ends the session after this much silence. */
  readonly timeoutMs: number
}

export interface StreamFrame {
  readonly stream: number
  /** Undefined for a stream byte the firmware does not publish. */
  readonly name?: StreamName
  /** The frame without its stream byte. */
  readonly data: Uint8Array
  /** The stream's own counter in the session header, so a gap is frames lost; undefined on the logs characteristic (no header). */
  readonly seq?: number
  /** When it arrived (ms since the epoch). */
  readonly at: number
}

export type LinkDiagnostic = { readonly kind: 'dropped'; readonly characteristic: number; readonly reason: string } | { readonly kind: 'unknown-stream'; readonly stream: number }

const sleep = (ms: number): Promise<void> => new Promise((resolve) => setTimeout(resolve, ms))

export class RunitLink {
  readonly defs: LinkDefinitions
  readonly commands: CommandClient
  private readonly policy: ReconnectPolicy | undefined
  private readonly packetBytes: number
  private readonly waitForLinkMs: number
  private readonly stateHandlers = new Set<(status: LinkStatus) => void>()
  private readonly streamHandlers = new Set<(frame: StreamFrame) => void>()
  private readonly diagnosticHandlers = new Set<(diagnostic: LinkDiagnostic) => void>()
  private readonly waiters = new Set<{ resolve: () => void; reject: (error: Error) => void }>()
  private readonly streamNames = new Map<number, StreamName>()
  private reassemblers = new Map<number, Reassembler>()
  private channel: BleChannel | undefined
  private tx: Promise<unknown> = Promise.resolve()
  private generation = 0
  private wanted = false
  private looping = false
  private current: BleDeviceRef | undefined
  private status: LinkStatus = { state: 'idle' }
  private sessionInfo: SessionInfo | undefined
  private helloWaiter: ((hello: SessionInfo) => void) | undefined
  private lastSentAt = 0
  private pinger: ReturnType<typeof setInterval> | undefined
  private reopening = false

  constructor(
    private readonly platform: BlePlatform,
    options: LinkOptions = {},
  ) {
    this.defs = options.defs ?? bundledLinkDefinitions()
    this.policy = options.reconnect === false ? undefined : { ...DEFAULT_RECONNECT, ...options.reconnect }
    this.packetBytes = Math.min(options.packetBytes ?? ATT_VALUE_MAX, ATT_VALUE_MAX)
    this.waitForLinkMs = options.waitForLinkMs ?? 5000
    for (const name of STREAM_NAMES) this.streamNames.set(this.defs.streams[name], name)
    this.commands = new CommandClient({
      defs: this.defs,
      send: (frame) => this.sendFrame(frame),
      address: () => ({ dst: this.sessionInfo?.boardId ?? 0, src: this.sessionInfo?.clientId ?? this.defs.session.id.none }),
      errors: options.errors ?? errorCatalog(),
      timeoutMs: options.timeoutMs,
      maxInFlight: options.maxInFlight,
      stallAfter: options.stallAfter,
      onStall: () => this.recycle('The board stopped answering.'),
      onSendFailed: (error) => this.recycle(`A write failed: ${errorText(error)}`),
    })
  }

  get state(): LinkState {
    return this.status.state
  }

  get device(): BleDeviceRef | undefined {
    return this.current
  }

  /** The open session; undefined while there is none. */
  get session(): SessionInfo | undefined {
    return this.sessionInfo
  }

  /* ---- connection ---- */

  /**
   * Opens the link to `device` and keeps it open. By default one attempt: a failure throws and the link stays idle (a chooser
   * the user just closed, a board that is off). With `retry` the attempts follow the reconnect policy until one works.
   */
  async connect(device: BleDeviceRef, options: { retry?: boolean } = {}): Promise<void> {
    if (this.status.state === 'closed') throw new CommandError('The link is closed.', 'closed')
    if (this.wanted) await this.disconnect()
    this.current = device
    this.wanted = true
    if (options.retry && this.policy) {
      void this.keepOpen(0)
      await this.whenConnected(Number.POSITIVE_INFINITY)
      return
    }
    this.setStatus({ state: 'connecting' })
    try {
      await this.openOnce()
    } catch (error) {
      this.wanted = false
      this.setStatus({ state: 'idle', reason: errorText(error) })
      throw error
    }
  }

  /** Drops the link on purpose and stops reconnecting. */
  async disconnect(): Promise<void> {
    this.wanted = false
    this.generation++
    const channel = this.channel
    this.channel = undefined
    this.endSession()
    this.reassemblers = new Map()
    this.commands.cancelAll('not-connected', 'The link was disconnected.')
    this.rejectWaiters('The link was disconnected.')
    if (this.status.state !== 'closed') this.setStatus({ state: 'idle' })
    await channel?.close().catch(() => undefined)
  }

  /** Drops the link and opens it again at once, for the board changed its GATT table and the OS may have dropped every handle. */
  reopen(): void {
    this.recycle('The link was reopened.')
  }

  async close(): Promise<void> {
    await this.disconnect()
    this.commands.close()
    this.setStatus({ state: 'closed' })
  }

  /* ---- commands ---- */

  /** The router's output for the request. Waits for a link that is coming back; fails `not-connected` when there is none to wait for. */
  async request(request: Request, options?: CommandOptions): Promise<Uint8Array> {
    await this.ready()
    return this.commands.request(request, options)
  }

  /* ---- events ---- */

  onStatus(handler: (status: LinkStatus) => void): () => void {
    this.stateHandlers.add(handler)
    return () => this.stateHandlers.delete(handler)
  }

  /** Every frame that is not an answer: telemetry, logs, errors, events. */
  onStream(handler: (frame: StreamFrame) => void): () => void {
    this.streamHandlers.add(handler)
    return () => this.streamHandlers.delete(handler)
  }

  /** The frames of one stream (`telemetry`, `logs`, `errors`, `events`), raw. */
  on(name: StreamName, handler: (data: Uint8Array, frame: StreamFrame) => void): () => void {
    return this.onStream((frame) => {
      if (frame.name === name) handler(frame.data, frame)
    })
  }

  onDiagnostic(handler: (diagnostic: LinkDiagnostic) => void): () => void {
    this.diagnosticHandlers.add(handler)
    return () => this.diagnosticHandlers.delete(handler)
  }

  /* ---- opening and keeping ---- */

  private async openOnce(): Promise<void> {
    const device = this.current
    if (!device) throw new Error('No board chosen.')
    const generation = ++this.generation
    this.reassemblers = new Map()
    const opening = this.platform.open(device, {
      onPacket: (characteristic, packet) => {
        if (generation === this.generation) this.onPacket(characteristic, packet)
      },
      onClosed: (reason) => {
        if (generation === this.generation) this.lost(reason)
      },
    })
    let timer: ReturnType<typeof setTimeout> | undefined
    const limit = this.policy?.openTimeoutMs ?? DEFAULT_RECONNECT.openTimeoutMs
    const expired = new Promise<never>((_, reject) => {
      timer = setTimeout(() => reject(new Error(`The board did not open within ${limit} ms.`)), limit)
    })
    let channel: BleChannel
    try {
      channel = await Promise.race([opening, expired])
    } catch (error) {
      void opening.then((late) => late.close()).catch(() => undefined) // an open that finishes after we gave up must not stay open
      throw error
    } finally {
      clearTimeout(timer)
    }
    if (generation !== this.generation || !this.wanted) {
      await channel.close().catch(() => undefined)
      throw new CommandError('The link was disconnected while it was opening.', 'not-connected')
    }
    this.channel = channel
    try {
      await this.openSession(limit)
    } catch (error) {
      if (this.channel === channel) this.channel = undefined
      this.endSession()
      await channel.close().catch(() => undefined)
      throw error
    }
    if (generation !== this.generation) throw new CommandError('The link dropped while the session opened.', 'not-connected')
    this.setStatus({ state: 'connected' })
    this.resolveWaiters()
  }

  /* ---- the session ---- */

  /** HELLO until the board answers with this client's id, then the streams' SUBSCRIBE; the PINGs start. */
  private async openSession(limitMs: number): Promise<void> {
    this.sessionInfo = undefined
    const answered = new Promise<SessionInfo>((resolve) => (this.helloWaiter = resolve))
    const deadline = Date.now() + limitMs
    let info: SessionInfo | undefined
    while (!info) {
      if (Date.now() > deadline) throw new Error(`The board did not open a session within ${limitMs} ms.`)
      await this.sendControl(encodeHello(this.defs, CLIENT_TIMEOUT_MS))
      info = await Promise.race([answered, sleep(HELLO_RETRY_MS).then(() => undefined)])
    }
    this.sessionInfo = info
    this.startPings()
    for (const name of SUBSCRIBED) {
      const keys = [this.defs.keys.conn!, this.defs.dir.write, CONN_SUBSCRIBE]
      await this.commands.request({ keys, payload: Uint8Array.of(this.defs.streams[name]) }, { label: `SUBSCRIBE ${name}` })
    }
  }

  private endSession(): void {
    this.sessionInfo = undefined
    this.helloWaiter = undefined
    clearInterval(this.pinger)
    this.pinger = undefined
  }

  /** A PING whenever nothing went to the board for a third of its timeout (less a margin for the radio). */
  private startPings(): void {
    clearInterval(this.pinger)
    this.pinger = setInterval(() => {
      const info = this.sessionInfo
      if (!info || !this.channel) return
      if (Date.now() - this.lastSentAt >= (info.timeoutMs / 3) * 0.7) void this.sendControl(Uint8Array.of(this.defs.session.control.ping)).catch(() => undefined)
    }, 100)
  }

  private sendControl(body: Uint8Array): Promise<void> {
    const src = this.sessionInfo?.clientId ?? this.defs.session.id.none
    return this.sendFrame(encodeFrame(this.defs, { kind: this.defs.session.kind.control, dst: this.sessionInfo?.boardId ?? 0, src, seq: 0 }, body))
  }

  private onControl(body: Uint8Array): void {
    const hello = decodeHello(this.defs, body)
    if (hello) {
      if (hello.version !== this.defs.session.version) return
      const known = this.sessionInfo
      if (hello.yourId !== this.defs.session.id.none) {
        const info: SessionInfo = { clientId: hello.yourId, boardId: hello.boardId, bootId: hello.bootId, timeoutMs: hello.timeoutMs || 3000 }
        if (this.helloWaiter) {
          this.helloWaiter(info)
          this.helloWaiter = undefined
        } else if (known && (known.bootId !== info.bootId || known.clientId !== info.clientId)) this.reopenSession('The board opened a new session.')
        else this.sessionInfo = info // a new timeout (the dead-man was armed)
      } else if (known && hello.bootId !== known.bootId) this.reopenSession('The board restarted.')
      return
    }
    if (body[0] === this.defs.session.control.noSession && this.sessionInfo) this.reopenSession('The board ended the session.')
  }

  /** The board has no session for this client any more: commands on the way are lost, the session opens again. */
  private reopenSession(reason: string): void {
    if (this.reopening || !this.channel) return
    this.reopening = true
    this.endSession()
    this.commands.cancelAll('link-lost', reason)
    const channel = this.channel
    this.setStatus({ state: 'reconnecting', reason, attempt: 0 })
    this.openSession(this.policy?.openTimeoutMs ?? DEFAULT_RECONNECT.openTimeoutMs)
      .then(() => {
        if (this.channel !== channel) return
        this.setStatus({ state: 'connected' })
        this.resolveWaiters()
      })
      .catch((error: unknown) => this.recycle(`The session did not open again: ${errorText(error)}`))
      .finally(() => (this.reopening = false))
  }

  /** The link dropped by itself. */
  private lost(reason: string, closing?: Promise<unknown>): void {
    if (!this.channel) return
    this.channel = undefined
    this.generation++
    this.endSession()
    this.reassemblers = new Map()
    this.commands.cancelAll('link-lost', `The link dropped: ${reason}`)
    if (this.wanted && this.policy) {
      this.setStatus({ state: 'reconnecting', reason, attempt: 0 })
      void this.keepOpen(0, closing)
    } else {
      this.wanted = false
      this.setStatus({ state: 'idle', reason })
      this.rejectWaiters(reason)
    }
  }

  private recycle(reason: string): void {
    const channel = this.channel
    if (!channel) return
    // the old link is closed before the new one opens: a platform that finds the board still connected would reuse it, and the
    // late close would then drop the new link
    this.lost(reason, channel.close().catch(() => undefined))
  }

  /** Attempts to open until one works, `disconnect()` is called, or the policy gives up. */
  private async keepOpen(first: number, before?: Promise<unknown>): Promise<void> {
    const policy = this.policy
    if (!policy || this.looping) return
    this.looping = true
    try {
      await before
      for (let attempt = first; this.wanted; attempt++) {
        this.setStatus({ state: 'reconnecting', reason: this.status.reason, attempt })
        const wait = policy.delaysMs[Math.min(attempt, policy.delaysMs.length - 1)] ?? 0
        if (wait > 0) await sleep(wait)
        if (!this.wanted) return
        try {
          await this.openOnce()
          return
        } catch (error) {
          this.setStatus({ state: 'reconnecting', reason: errorText(error), attempt })
          if (attempt + 1 >= policy.maxAttempts) {
            this.wanted = false
            this.setStatus({ state: 'idle', reason: errorText(error) })
            this.rejectWaiters(errorText(error))
            return
          }
        }
      }
    } finally {
      this.looping = false
    }
  }

  /* ---- frames ---- */

  private sendFrame(frame: Uint8Array): Promise<void> {
    const channel = this.channel
    if (!channel) return Promise.reject(new Error('The link is down.'))
    const generation = this.generation
    const packets = fragment(this.defs, frame, this.packetBytes)
    const run = async () => {
      for (const packet of packets) {
        if (generation !== this.generation) throw new Error('The link changed while the request was being written.')
        await channel.write(packet)
      }
    }
    const result = this.tx.then(run)
    this.tx = result.catch(() => undefined)
    this.lastSentAt = Date.now()
    return result
  }

  private onPacket(characteristic: number, packet: Uint8Array): void {
    let reassembler = this.reassemblers.get(characteristic)
    if (!reassembler) {
      reassembler = new Reassembler(this.defs)
      this.reassemblers.set(characteristic, reassembler)
    }
    const result = reassembler.push(packet)
    if (!result) return
    if ('dropped' in result) {
      this.diagnosticHandlers.forEach((h) => h({ kind: 'dropped', characteristic, reason: result.dropped }))
      return
    }
    // The LOGS characteristic carries the logs stream's payloads as they are: no session header.
    if (characteristic === this.defs.gatt.logs) {
      this.emitStream(this.defs.streams.logs, result.frame)
      return
    }
    const frame = decodeFrame(this.defs, result.frame)
    if (!frame) return
    if (frame.kind === this.defs.session.kind.control) this.onControl(frame.body)
    else if (frame.kind === this.defs.session.kind.answer) this.commands.onResponse(decodeResponse(this.defs, result.frame)!)
    else if (frame.kind === this.defs.session.kind.stream && frame.body.length > 0) this.emitStream(frame.body[0]!, frame.body.subarray(1), frame.seq)
  }

  private emitStream(stream: number, data: Uint8Array, seq?: number): void {
    const name = this.streamNames.get(stream)
    if (!name) this.diagnosticHandlers.forEach((h) => h({ kind: 'unknown-stream', stream }))
    const frame: StreamFrame = { stream, ...(name ? { name } : {}), data, ...(seq !== undefined ? { seq } : {}), at: Date.now() }
    this.streamHandlers.forEach((h) => h(frame))
  }

  /* ---- waiting for the link ---- */

  private ready(): Promise<void> {
    if (this.status.state === 'connected') return Promise.resolve()
    if (this.status.state === 'connecting' || this.status.state === 'reconnecting') return this.whenConnected(this.waitForLinkMs)
    return Promise.reject(new CommandError('The board is not connected.', 'not-connected'))
  }

  private whenConnected(ms: number): Promise<void> {
    if (this.status.state === 'connected') return Promise.resolve()
    return new Promise((resolve, reject) => {
      const timer = Number.isFinite(ms) ? setTimeout(() => waiter.reject(new CommandError(`The board did not come back within ${ms} ms.`, 'not-connected')), ms) : undefined
      const waiter = {
        resolve: () => {
          clearTimeout(timer)
          this.waiters.delete(waiter)
          resolve()
        },
        reject: (error: Error) => {
          clearTimeout(timer)
          this.waiters.delete(waiter)
          reject(error)
        },
      }
      this.waiters.add(waiter)
    })
  }

  private resolveWaiters(): void {
    for (const waiter of [...this.waiters]) waiter.resolve()
  }

  private rejectWaiters(reason: string): void {
    for (const waiter of [...this.waiters]) waiter.reject(new CommandError(reason, 'not-connected'))
  }

  private setStatus(status: LinkStatus): void {
    const same = status.state === this.status.state && status.attempt === this.status.attempt && status.reason === this.status.reason
    this.status = status
    if (!same) this.stateHandlers.forEach((handler) => handler(status))
  }
}

const errorText = (error: unknown): string => (error instanceof Error ? error.message : String(error))
