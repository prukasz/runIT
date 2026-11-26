import type { ServiceCatalog } from '../catalog/services'
import { bundledLinkDefinitions, bundledServiceCatalog, errorCatalog, STREAM_NAMES } from '../definitions'
import type { ErrorInfo, LinkDefinitions, StreamName } from '../definitions'
import { serviceCall } from '../protocol/calls'
import { decodeFrame, decodeHello, encodeControl, encodeHello } from '../protocol/frame'
import type { Frame, Hello, Request } from '../protocol/frame'
import { fragment, Reassembler } from '../transport/framing'
import type { BleChannel, BleDeviceRef, BlePlatform, OpenHandlers, OpenSpec } from '../transport/types'
import { CommandClient, CommandError } from './commands'
import type { CommandOptions } from './commands'
import { DEADMAN_COMMANDS } from './deadman'
import { peersCall, readPeers } from './sessions'

/*
 * The link to one board over BLE: the session (SYS_SESSION.MD), the commands, the streams, and a connection that keeps itself up.
 *
 *   - Opening is one call of the platform: the runIT service, written on its session characteristic (framed), heard on
 *     the same one and on the raw ones the board has (`logs`: the log lines, `telemetry`: the
 *     watches' entries). Then the session: this client's HELLO (its id as the source, a boot id of its own), until the board
 *     answers with its HELLO (its id, boot id, build), then SESSION PEERS for this session's dead-man. Nothing is subscribed:
 *     the board's setup links its errors and events to the session and its logs and watches to their characteristics.
 *   - Every frame has the header of CMD.MD: an ANSWER settles its request (seq and key), a PUSH is a stream by its key (a
 *     module's number: `errors`, `events`, `logs`, `watches`, `remote`), a CONTROL is the session's own. A raw characteristic
 *     carries one whole frame per notification; the session's are fragmented (transport/framing.ts).
 *   - With the dead-man on, something goes to the board at least every third of its time (a PING when nothing else does). A
 *     NO_SESSION or a HELLO with a new boot id (the board restarted, or ended the session) fails the requests on the way and opens
 *     the session again.
 *   - A link that drops by itself is reopened at once (the first attempt has no delay), the delays growing to a ceiling, until
 *     `disconnect()`. A request on the wire when it drops fails `link-lost` (it may have run); one given while the link comes back
 *     waits for it, up to `waitForLinkMs`.
 *   - A write that fails recycles the link, and so do requests that stop being answered (`stallAfter` timeouts in a row): after
 *     a GATT change the OS invalidates the handles and reports no disconnect.
 *   - The packets of one frame never interleave with another's: the board reassembles with one buffer.
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
  /** One attempt that neither works nor fails is dropped after this long. */
  readonly openTimeoutMs: number
}

export const DEFAULT_RECONNECT: ReconnectPolicy = { delaysMs: [0, 100, 250, 500, 1000, 2000], maxAttempts: Number.POSITIVE_INFINITY, openTimeoutMs: 10_000 }

/** The longest value of a GATT attribute (BLE Core spec): the largest packet one write carries. */
export const ATT_VALUE_MAX = 512

export interface LinkOptions {
  readonly defs?: LinkDefinitions
  readonly errors?: ReadonlyMap<number, ErrorInfo>
  /** The catalog SESSION PEERS is read with (default the bundled one). */
  readonly catalog?: ServiceCatalog
  /** Bytes of a packet written to the board, its header byte included; at most and by default `ATT_VALUE_MAX`. */
  readonly packetBytes?: number
  /** Default wait for an answer (ms). */
  readonly timeoutMs?: number
  readonly maxInFlight?: number
  /** Timeouts in a row after which the link is recycled (default 3). */
  readonly stallAfter?: number
  /** `false`: a lost link stays lost. */
  readonly reconnect?: Partial<ReconnectPolicy> | false
  /** How long a request waits for a link that is coming back (default 5000 ms). */
  readonly waitForLinkMs?: number
  /** The id this client says (default the first client id, the app's; another client takes one of `defs.src.clientFirst..clientLast`). */
  readonly clientId?: number
}

/** The session as the board's HELLO and PEERS gave it. */
export interface SessionInfo {
  /** This client's id, the source of its frames. */
  readonly clientId: number
  /** The board's id (its place in the project). */
  readonly boardId: number
  /** Random per boot of the board: another one is a restart. */
  readonly bootId: number
  /** The board's build: the first 4 bytes of its firmware's ELF SHA-256. */
  readonly buildId: number
  /** The board ends this session after this much silence; 0: never. */
  readonly deadmanMs: number
}

export interface StreamFrame {
  /** The stream's key: a module's number. */
  readonly key: number
  /** Undefined for a key that is no stream the definitions name. */
  readonly name?: StreamName
  /** Who made it: the board, or another board whose frame it passed on. */
  readonly src: number
  /** The session's count of its pushes of that class (`urgent`): a gap is frames lost; undefined on a raw characteristic. */
  readonly seq?: number
  readonly urgent: boolean
  readonly data: Uint8Array
  /** When it arrived (ms since the epoch). */
  readonly at: number
}

export type LinkDiagnostic =
  /** Packets of a framed characteristic out of order: the rest of that frame is lost. */
  | { readonly kind: 'dropped'; readonly characteristic: number; readonly reason: string }
  /** A PUSH whose key is no stream the definitions name. */
  | { readonly kind: 'unknown-stream'; readonly key: number }
  /** A notification that is no frame this client takes. */
  | { readonly kind: 'not-a-frame'; readonly characteristic: number; readonly length: number }

/** A HELLO that gets no answer is sent again this often while the session opens. */
const HELLO_RETRY_MS = 500

const sleep = (ms: number): Promise<void> => new Promise((resolve) => setTimeout(resolve, ms))
const errorText = (error: unknown): string => (error instanceof Error ? error.message : String(error))

export class RunitLink {
  readonly defs: LinkDefinitions
  readonly commands: CommandClient
  readonly clientId: number
  private readonly catalog: ServiceCatalog
  private readonly policy: ReconnectPolicy | undefined
  private readonly packetBytes: number
  private readonly waitForLinkMs: number
  private readonly spec: OpenSpec
  private readonly streamNames: ReadonlyMap<number, StreamName>
  private readonly statusHandlers = new Set<(status: LinkStatus) => void>()
  private readonly streamHandlers = new Set<(frame: StreamFrame) => void>()
  private readonly diagnosticHandlers = new Set<(diagnostic: LinkDiagnostic) => void>()
  private readonly waiters = new Set<{ resolve: () => void; reject: (error: Error) => void }>()
  private readonly reassembler: Reassembler
  /** This client's boot id, one per link: the board sees another one as a client that started over. */
  private readonly bootId = Math.floor(Math.random() * 0x100000000) >>> 0
  private channel: BleChannel | undefined
  /** The other characteristics of the service someone took over (`tunnel`): their packets are not this link's frames. */
  private readonly tunnels = new Map<number, OpenHandlers>()
  private tx: Promise<unknown> = Promise.resolve()
  private generation = 0
  private wanted = false
  private looping = false
  private reopening = false
  private current: BleDeviceRef | undefined
  private status: LinkStatus = { state: 'idle' }
  private sessionInfo: SessionInfo | undefined
  private helloWaiter: ((hello: Hello) => void) | undefined
  private lastSentAt = 0
  private pinger: ReturnType<typeof setInterval> | undefined

  constructor(
    private readonly platform: BlePlatform,
    options: LinkOptions = {},
  ) {
    this.defs = options.defs ?? bundledLinkDefinitions()
    this.catalog = options.catalog ?? bundledServiceCatalog()
    this.clientId = options.clientId ?? this.defs.src.clientFirst
    const { src } = this.defs
    if (!Number.isInteger(this.clientId) || this.clientId < src.clientFirst || this.clientId > src.clientLast) throw new RangeError(`A client's id is ${src.clientFirst}..${src.clientLast}, not ${this.clientId}.`)
    this.policy = options.reconnect === false ? undefined : { ...DEFAULT_RECONNECT, ...options.reconnect }
    this.packetBytes = Math.min(options.packetBytes ?? ATT_VALUE_MAX, ATT_VALUE_MAX)
    this.waitForLinkMs = options.waitForLinkMs ?? 5000
    const { gatt } = this.defs
    this.spec = { service: gatt.service, write: gatt.session, notify: [gatt.session], optional: [gatt.logs, gatt.telemetry, gatt.bridge] }
    this.streamNames = new Map(STREAM_NAMES.map((name) => [this.defs.streams[name], name]))
    this.reassembler = new Reassembler(this.defs)
    this.commands = new CommandClient({
      defs: this.defs,
      src: this.clientId,
      send: (frame) => this.sendFrame(frame),
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

  /**
   * Sets the dead-man of this client's session on the board (SYS DEADMAN: 0 is off, else 150..60000 ms; its end enters the safe
   * state) and keeps the pings to it: a PING whenever nothing was sent for a third of it.
   */
  async setDeadman(ms: number): Promise<void> {
    if (!this.sessionInfo) throw new CommandError('There is no session with the board.', 'closed')
    const call = serviceCall(this.catalog, DEADMAN_COMMANDS.service, DEADMAN_COMMANDS.set, { [DEADMAN_COMMANDS.timeout]: ms }, 'write')
    await this.commands.request(call.request, { label: call.label })
    if (!this.sessionInfo) return
    this.sessionInfo = { ...this.sessionInfo, deadmanMs: ms }
    this.startPings()
  }

  /* ---- connection ---- */

  /**
   * Opens the link to `device` and keeps it open. By default one attempt: a failure throws and the link stays idle (a chooser the
   * user closed, a board that is off). With `retry` the attempts follow the reconnect policy until one works.
   */
  async connect(device: BleDeviceRef, options: { retry?: boolean } = {}): Promise<void> {
    if (this.status.state === 'closed') throw new CommandError('The link is closed.', 'closed')
    if (this.wanted) await this.disconnect()
    this.current = device
    this.wanted = true
    if (options.retry && this.policy) {
      void this.keepOpen(undefined, 'connecting')
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
    this.closeTunnels('The link was disconnected.')
    this.endSession()
    this.reassembler.reset()
    this.commands.cancelAll('not-connected', 'The link was disconnected.')
    this.rejectWaiters('The link was disconnected.')
    if (this.status.state !== 'closed') this.setStatus({ state: 'idle' })
    await channel?.close().catch(() => undefined)
  }

  /** Drops the link and opens it again at once: the board changed its GATT table and the OS may have dropped every handle. */
  reopen(): void {
    this.recycle('The link was reopened.')
  }

  async close(): Promise<void> {
    await this.disconnect()
    this.commands.close()
    this.setStatus({ state: 'closed' })
  }

  /* ---- another characteristic ---- */

  /**
   * Takes over another characteristic of the open service (the bridge: a tunnel to another board): its packets go to
   * `handlers`, and what is written to the returned channel goes to it, in turn with this link's own packets. It lasts as long
   * as this connection: a link that drops or is disconnected closes it (`onClosed`). One taker per characteristic: a new one
   * replaces it.
   */
  tunnel(characteristic: number, handlers: OpenHandlers): BleChannel {
    if (!this.channel || this.status.state !== 'connected') throw new CommandError('The board is not connected.', 'not-connected')
    const generation = this.generation
    this.tunnels.set(characteristic, handlers)
    return {
      write: (packet) => {
        if (this.tunnels.get(characteristic) !== handlers) return Promise.reject(new Error('The tunnel is closed.'))
        return this.writePackets([packet], generation, characteristic)
      },
      close: async () => {
        if (this.tunnels.get(characteristic) === handlers) this.tunnels.delete(characteristic)
      },
    }
  }

  private closeTunnels(reason: string): void {
    const closed = [...this.tunnels.values()]
    this.tunnels.clear()
    for (const handlers of closed) handlers.onClosed(reason)
  }

  /* ---- requests ---- */

  /** The router's output for the request. Waits for a link that is coming back; fails `not-connected` when there is none to wait for. */
  async request(request: Request, options?: CommandOptions): Promise<Uint8Array> {
    await this.ready()
    return this.commands.request(request, options)
  }

  /* ---- events ---- */

  onStatus(handler: (status: LinkStatus) => void): () => void {
    this.statusHandlers.add(handler)
    return () => this.statusHandlers.delete(handler)
  }

  /** Every PUSH frame, of any stream. */
  onStream(handler: (frame: StreamFrame) => void): () => void {
    this.streamHandlers.add(handler)
    return () => this.streamHandlers.delete(handler)
  }

  /** The frames of one stream, their data. */
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
    this.reassembler.reset()
    const opening = this.platform.open(device, this.spec, {
      onPacket: (characteristic, packet) => {
        if (generation === this.generation) this.onPacket(characteristic, packet)
      },
      onClosed: (reason) => {
        if (generation === this.generation) this.lost(reason)
      },
    })
    const limit = this.policy?.openTimeoutMs ?? DEFAULT_RECONNECT.openTimeoutMs
    let timer: ReturnType<typeof setTimeout> | undefined
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

  /** HELLO until the board answers with its own, then SESSION PEERS for this session's dead-man; the PINGs start. */
  private async openSession(limitMs: number): Promise<void> {
    this.sessionInfo = undefined
    const answered = new Promise<Hello>((resolve) => (this.helloWaiter = resolve))
    const deadline = Date.now() + limitMs
    let hello: Hello | undefined
    while (!hello) {
      if (Date.now() > deadline) throw new Error(`The board did not open a session within ${limitMs} ms.`)
      await this.sendFrame(encodeHello(this.defs, { id: this.clientId, bootId: this.bootId }))
      hello = await Promise.race([answered, sleep(HELLO_RETRY_MS).then(() => undefined)])
    }
    this.sessionInfo = { clientId: this.clientId, boardId: hello.id, bootId: hello.bootId, buildId: hello.buildId, deadmanMs: 0 }
    const call = peersCall(this.catalog)
    const peers = readPeers(call, await this.commands.request(call.request, { label: call.label }), this.defs.src.none)
    const mine = peers.find((peer) => peer.open && peer.peerId === this.clientId)
    this.sessionInfo = { ...this.sessionInfo, deadmanMs: mine?.deadmanMs ?? 0 }
    this.startPings()
  }

  private endSession(): void {
    this.sessionInfo = undefined
    this.helloWaiter = undefined
    clearInterval(this.pinger)
    this.pinger = undefined
  }

  /** With the board's dead-man on: a PING whenever nothing went to the board for a third of its time (less a margin for the radio). */
  private startPings(): void {
    clearInterval(this.pinger)
    this.pinger = undefined
    const time = this.sessionInfo?.deadmanMs ?? 0
    if (time <= 0) return
    this.pinger = setInterval(() => {
      if (!this.sessionInfo || !this.channel) return
      if (Date.now() - this.lastSentAt >= (time / 3) * 0.7) void this.sendFrame(encodeControl(this.defs, this.clientId, this.defs.control.ping)).catch(() => undefined)
    }, Math.max(20, time / 10))
  }

  private onControl(frame: Frame): void {
    const hello = decodeHello(this.defs, frame)
    if (hello) {
      const known = this.sessionInfo
      if (this.helloWaiter) {
        this.helloWaiter(hello)
        this.helloWaiter = undefined
      } else if (known && (known.bootId !== hello.bootId || known.boardId !== hello.id)) this.reopenSession('The board restarted.')
      return
    }
    if (frame.data[0] === this.defs.control.noSession && this.sessionInfo) this.reopenSession('The board ended the session.')
  }

  /** The board has no session with this client any more: the requests on the way are lost, the session opens again. */
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

  /** The link dropped by itself; `closing` is the close of the old link, awaited before a new one opens. */
  private lost(reason: string, closing?: Promise<unknown>): void {
    if (!this.channel) return
    this.channel = undefined
    this.generation++
    this.closeTunnels(reason)
    this.endSession()
    this.reassembler.reset()
    this.commands.cancelAll('link-lost', `The link dropped: ${reason}`)
    if (this.wanted && this.policy) {
      this.setStatus({ state: 'reconnecting', reason, attempt: 0 })
      void this.keepOpen(closing)
    } else {
      this.wanted = false
      this.setStatus({ state: 'idle', reason })
      this.rejectWaiters(reason)
    }
  }

  /** Closes the link and opens it again: a platform that finds the board still connected would reuse the old one, and its late close drop the new. */
  private recycle(reason: string): void {
    const channel = this.channel
    if (channel) this.lost(reason, channel.close().catch(() => undefined))
  }

  /** Attempts to open until one works, `disconnect()` is called, or the policy gives up; `state` is what the attempts are shown as (a first connection is not a lost one). */
  private async keepOpen(before?: Promise<unknown>, state: 'connecting' | 'reconnecting' = 'reconnecting'): Promise<void> {
    const policy = this.policy
    if (!policy || this.looping) return
    this.looping = true
    try {
      await before
      for (let attempt = 0; this.wanted; attempt++) {
        this.setStatus({ state, reason: this.status.reason, attempt })
        const wait = policy.delaysMs[Math.min(attempt, policy.delaysMs.length - 1)] ?? 0
        if (wait > 0) await sleep(wait)
        if (!this.wanted) return
        try {
          await this.openOnce()
          return
        } catch (error) {
          // Disconnected meanwhile: the link is idle, an attempt that failed for it says nothing.
          if (!this.wanted) return
          this.setStatus({ state, reason: errorText(error), attempt })
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
    if (!this.channel) return Promise.reject(new Error('The link is down.'))
    this.lastSentAt = Date.now()
    return this.writePackets(fragment(this.defs, frame, this.packetBytes), this.generation)
  }

  /** The packets one after another, after what is already on its way; to `characteristic`, else the session's. */
  private writePackets(packets: readonly Uint8Array[], generation: number, characteristic?: number): Promise<void> {
    const channel = this.channel
    if (!channel) return Promise.reject(new Error('The link is down.'))
    const result = this.tx.then(async () => {
      for (const packet of packets) {
        if (generation !== this.generation) throw new Error('The link changed while the frame was being written.')
        await (characteristic === undefined ? channel.write(packet) : channel.write(packet, characteristic))
      }
    })
    this.tx = result.catch(() => undefined)
    return result
  }

  /** The session's characteristic is framed; the others carry one whole frame per notification. */
  private onPacket(characteristic: number, packet: Uint8Array): void {
    const tunnel = this.tunnels.get(characteristic)
    if (tunnel) return tunnel.onPacket(characteristic, packet)
    if (characteristic === this.defs.gatt.bridge) return // a tunnel nobody takes: what still comes through it is not this link's
    let bytes = packet
    if (characteristic === this.defs.gatt.session) {
      const result = this.reassembler.push(packet)
      if (!result) return
      if ('dropped' in result) return this.diagnose({ kind: 'dropped', characteristic, reason: result.dropped })
      bytes = result.frame
    }
    const frame = decodeFrame(this.defs, bytes)
    const { kind } = this.defs
    if (!frame) return this.diagnose({ kind: 'not-a-frame', characteristic, length: bytes.length })
    const session = characteristic === this.defs.gatt.session
    if (session && frame.kind === kind.control) this.onControl(frame)
    else if (session && frame.kind === kind.answer) this.commands.onAnswer(frame)
    else if (frame.kind === kind.push) this.emitStream(frame, session)
    else this.diagnose({ kind: 'not-a-frame', characteristic, length: bytes.length })
  }

  private emitStream(frame: Frame, session: boolean): void {
    const name = this.streamNames.get(frame.key)
    if (!name) this.diagnose({ kind: 'unknown-stream', key: frame.key })
    const stream: StreamFrame = {
      key: frame.key,
      ...(name ? { name } : {}),
      src: frame.src,
      ...(session ? { seq: frame.seq } : {}),
      urgent: (frame.flags & this.defs.flags.urgent) !== 0,
      data: frame.data,
      at: Date.now(),
    }
    this.streamHandlers.forEach((handler) => handler(stream))
  }

  private diagnose(diagnostic: LinkDiagnostic): void {
    this.diagnosticHandlers.forEach((handler) => handler(diagnostic))
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
    if (!same) this.statusHandlers.forEach((handler) => handler(status))
  }
}
