import type { OutgoingMessage, ReceivedFrame, ReceivedFrameHandler, StreamRoute, StreamTargetId } from '../stream'
import { decodeInterfaceResponse, encodeCommandFrame, RUNIT_ROUTE } from './frames'
import type { InterfaceErrorData, InterfaceProtocol, InterfaceResponseFrame } from './frames'

export type CommandErrorCode = 'timeout' | 'cancelled' | 'send-failed' | 'device-error'

/** A command that got no usable answer, or (device-error) was refused by the board. */
export class CommandError extends Error {
  readonly code: CommandErrorCode
  readonly seq?: number
  readonly response?: CommandResponse

  constructor(message: string, code: CommandErrorCode, details: { seq?: number; response?: CommandResponse; cause?: unknown } = {}) {
    super(message, details.cause === undefined ? undefined : { cause: details.cause })
    this.name = 'CommandError'
    this.code = code
    this.seq = details.seq
    this.response = details.response
  }
}

/** What the client needs from the router: send one message. */
export interface CommandSender {
  send(message: OutgoingMessage): Promise<unknown>
}

/** What the client needs from the ingress stream: every received frame. */
export interface CommandFrameSource {
  subscribe(handler: ReceivedFrameHandler): () => void
}

export interface CommandClientOptions {
  readonly sender: CommandSender
  readonly frames: CommandFrameSource
  /** Response stream byte and OK status, from the generated descriptors. */
  readonly protocol: InterfaceProtocol
  /** Board the commands go to; only its frames are read. */
  readonly targetId: StreamTargetId
  readonly route?: StreamRoute
  /** Per-command wait for the answer, counted from when the write completed. */
  readonly timeoutMs?: number
  /**
   * Commands on the wire at once. The board answers one at a time in order,
   * so 1 (the default) costs little and keeps a slow command from delaying
   * a queue of others past their timeouts.
   */
  readonly maxInFlight?: number
  readonly now?: () => number
}

export interface CommandRequest {
  /** `[class][packet][payload]`, without the sequence byte. */
  readonly body: Uint8Array
  readonly timeoutMs?: number
  /** Shown in the observer events, e.g. the contract ID. */
  readonly label?: string
}

export interface CommandResponse extends InterfaceResponseFrame {
  readonly ok: boolean
  readonly label?: string
  /** When the write started; receivedAt - sentAt is the round trip. */
  readonly sentAt: number
  readonly receivedAt: number
}

export type CommandEvent =
  | { readonly type: 'sent'; readonly seq: number; readonly frame: Uint8Array; readonly label?: string; readonly at: number }
  | { readonly type: 'response'; readonly response: CommandResponse }
  | { readonly type: 'timeout'; readonly seq: number; readonly label?: string; readonly at: number }
  /** An answer no pending command waits for, e.g. a late one after a timeout. */
  | { readonly type: 'unmatched'; readonly frame: InterfaceResponseFrame; readonly at: number }

export type CommandEventHandler = (event: CommandEvent) => void

interface Pending {
  readonly request: CommandRequest
  readonly resolve: (response: CommandResponse) => void
  readonly reject: (error: Error) => void
  seq?: number
  sentAt?: number
  timer?: ReturnType<typeof setTimeout>
}

const DEFAULT_TIMEOUT_MS = 2000

/**
 * Sends runIT commands and matches each answer by its sequence byte
 * (`[seq][class][packet][payload]` → `[stream][seq][class][packet][status][data]`).
 *
 * send() resolves with every answer, OK or ERROR; call() also rejects on
 * ERROR. Both reject with CommandError on timeout, send failure or close().
 */
export class CommandClient {
  private readonly sender: CommandSender
  private readonly protocol: InterfaceProtocol
  private readonly targetId: StreamTargetId
  private readonly route: StreamRoute
  private readonly timeoutMs: number
  private readonly maxInFlight: number
  private readonly now: () => number
  private readonly queue: Pending[] = []
  private readonly inFlight = new Map<number, Pending>()
  private readonly observers = new Set<CommandEventHandler>()
  private readonly stopFrames: () => void
  private lastSeq = 0xff
  private closed = false

  constructor(options: CommandClientOptions) {
    this.sender = options.sender
    this.protocol = options.protocol
    this.targetId = options.targetId
    this.route = options.route ?? RUNIT_ROUTE.interface
    this.timeoutMs = options.timeoutMs ?? DEFAULT_TIMEOUT_MS
    this.maxInFlight = Math.max(1, Math.min(255, options.maxInFlight ?? 1))
    this.now = options.now ?? Date.now
    this.stopFrames = options.frames.subscribe((frame) => this.onFrame(frame))
  }

  /** Resolves with the board's answer, OK or ERROR. */
  send(request: CommandRequest): Promise<CommandResponse> {
    if (this.closed) return Promise.reject(new CommandError('The command client is closed.', 'cancelled'))
    if (request.body.byteLength < 1) return Promise.reject(new RangeError('A command needs at least its class byte.'))
    return new Promise<CommandResponse>((resolve, reject) => {
      this.queue.push({ request, resolve, reject })
      this.pump()
    })
  }

  /** Like send(), but an ERROR answer rejects with CommandError('device-error'). */
  async call(request: CommandRequest): Promise<CommandResponse> {
    const response = await this.send(request)
    if (!response.ok) {
      throw new CommandError(`Command ${request.label ?? hexByte(response.requestClass, response.requestPacket)} failed: ${describeError(response.error)}.`, 'device-error', { seq: response.seq, response })
    }
    return response
  }

  observe(handler: CommandEventHandler): () => void {
    this.observers.add(handler)
    return () => this.observers.delete(handler)
  }

  get pendingCount(): number {
    return this.queue.length + this.inFlight.size
  }

  /** Reject everything queued or waiting and stop listening. */
  close(reason = 'The command client was closed.'): void {
    if (this.closed) return
    this.closed = true
    this.stopFrames()
    const pending = [...this.inFlight.values(), ...this.queue.splice(0)]
    this.inFlight.clear()
    for (const entry of pending) {
      clearTimeout(entry.timer)
      entry.reject(new CommandError(reason, 'cancelled', { seq: entry.seq }))
    }
  }

  private pump(): void {
    while (!this.closed && this.queue.length && this.inFlight.size < this.maxInFlight) {
      const entry = this.queue.shift()!
      entry.seq = this.nextSeq()
      this.inFlight.set(entry.seq, entry)
      void this.write(entry)
    }
  }

  /** Next free sequence byte. Counting on (not reusing the lowest free one) keeps a late answer from matching a new command. */
  private nextSeq(): number {
    for (let step = 1; step <= 0x100; step += 1) {
      const seq = (this.lastSeq + step) & 0xff
      if (!this.inFlight.has(seq)) {
        this.lastSeq = seq
        return seq
      }
    }
    throw new Error('No free sequence byte.') // unreachable: maxInFlight <= 255
  }

  private async write(entry: Pending): Promise<void> {
    const seq = entry.seq!
    const frame = encodeCommandFrame(seq, entry.request.body)
    entry.sentAt = this.now()
    try {
      await this.sender.send({ targetId: this.targetId, route: this.route, data: frame, delivery: 'reliable', correlationId: String(seq) })
    } catch (error) {
      if (this.inFlight.get(seq) !== entry) return // closed meanwhile
      this.inFlight.delete(seq)
      entry.reject(new CommandError(`Sending command seq ${seq} failed: ${error instanceof Error ? error.message : String(error)}`, 'send-failed', { seq, cause: error }))
      this.pump()
      return
    }
    this.emit({ type: 'sent', seq, frame, label: entry.request.label, at: entry.sentAt })
    if (this.inFlight.get(seq) !== entry) return // answered before the write resolved, or closed
    entry.timer = setTimeout(() => this.expire(seq, entry), entry.request.timeoutMs ?? this.timeoutMs)
  }

  private expire(seq: number, entry: Pending): void {
    if (this.inFlight.get(seq) !== entry) return
    this.inFlight.delete(seq)
    this.emit({ type: 'timeout', seq, label: entry.request.label, at: this.now() })
    entry.reject(new CommandError(`No answer to command seq ${seq} within ${entry.request.timeoutMs ?? this.timeoutMs} ms.`, 'timeout', { seq }))
    this.pump()
  }

  private onFrame(frame: ReceivedFrame): void {
    if (frame.targetId !== this.targetId) return
    const decoded = decodeInterfaceResponse(frame.data, this.protocol)
    if (!decoded) return
    const entry = this.inFlight.get(decoded.seq)
    const body = entry?.request.body
    // The board echoes class and packet; a mismatch is a stale answer to an earlier use of this seq.
    if (!entry || !body || decoded.requestClass !== body[0] || (body.byteLength > 1 && decoded.requestPacket !== body[1])) {
      this.emit({ type: 'unmatched', frame: decoded, at: frame.receivedAt })
      return
    }
    clearTimeout(entry.timer)
    this.inFlight.delete(decoded.seq)
    const response: CommandResponse = {
      ...decoded,
      ok: decoded.status === this.protocol.statusOk,
      label: entry.request.label,
      sentAt: entry.sentAt ?? frame.receivedAt,
      receivedAt: frame.receivedAt,
    }
    this.emit({ type: 'response', response })
    entry.resolve(response)
    this.pump()
  }

  private emit(event: CommandEvent): void {
    this.observers.forEach((handler) => handler(event))
  }
}

const hexByte = (...values: number[]): string => values.map((value) => value.toString(16).padStart(2, '0').toUpperCase()).join('/')

export const describeError = (error: InterfaceErrorData | undefined): string =>
  error ? `tag 0x${error.tag.toString(16).padStart(4, '0').toUpperCase()}, owner 0x${error.owner.toString(16).padStart(4, '0').toUpperCase()}` : 'error without tag data'
