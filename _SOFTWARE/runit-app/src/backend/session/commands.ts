import type { ErrorInfo, LinkDefinitions } from '../definitions'
import { decodeErrorRecord } from '../protocol/errors'
import type { ErrorRecord } from '../protocol/errors'
import { encodeRequest } from '../protocol/frame'
import type { Frame, Request } from '../protocol/frame'

/*
 * Commands over a link: a request gets a sequence number (the header's u16), its answer comes back with the same key and seq
 * (answers may come out of order: urgent requests overtake), and one that gets no answer times out. Several requests can be on
 * the wire at once (`maxInFlight`): the board runs normal requests in the order they came, so pipelining saves a round trip per
 * request and reorders nothing. An urgent request goes before every normal one still queued and is written even when the window
 * is full: STOP and the dead-man's PING must not wait behind requests the board is slow to answer. The sequence counts on
 * instead of reusing the lowest free number, so a late answer cannot match a newer request.
 */

export type CommandErrorCode =
  /** No answer in time. */
  | 'timeout'
  /** The link dropped while the request was queued or on the wire: it may or may not have run. */
  | 'link-lost'
  /** There was no link to send on. */
  | 'not-connected'
  /** A write failed: the request did not reach the board (a frame runs only when its last packet arrives), so it can be repeated. */
  | 'send-failed'
  /** The board answered ERROR: `record` says why. */
  | 'device-error'
  | 'too-long'
  | 'closed'

export class CommandError extends Error {
  constructor(
    message: string,
    readonly code: CommandErrorCode,
    readonly details: { readonly seq?: number; readonly record?: ErrorRecord; readonly cause?: unknown } = {},
  ) {
    super(message, details.cause === undefined ? undefined : { cause: details.cause })
    this.name = 'CommandError'
  }

  get record(): ErrorRecord | undefined {
    return this.details.record
  }
}

export interface CommandOptions {
  /** Wait for the answer, counted from when the request was written. */
  readonly timeoutMs?: number
  /** Shown in the observer events and the messages. */
  readonly label?: string
}

export type CommandEvent =
  | { readonly type: 'sent'; readonly seq: number; readonly frame: Uint8Array; readonly label?: string; readonly at: number }
  | { readonly type: 'answer'; readonly seq: number; readonly ok: boolean; readonly data: Uint8Array; readonly label?: string; readonly sentAt: number; readonly at: number }
  | { readonly type: 'timeout'; readonly seq: number; readonly label?: string; readonly at: number }
  /** An answer nobody waits for (late after a timeout, or not to a request of this client). */
  | { readonly type: 'unmatched'; readonly seq: number; readonly key: number; readonly at: number }

export interface CommandClientOptions {
  readonly defs: LinkDefinitions
  /** This client's id: the source of its requests. */
  readonly src: number
  /** Puts one whole frame on the link; rejects when it could not. */
  readonly send: (frame: Uint8Array) => Promise<void>
  /** Default wait for an answer (default 2000 ms). */
  readonly timeoutMs?: number
  /** Requests on the wire at once (default 4). */
  readonly maxInFlight?: number
  /** Names the errors of an ERROR answer (errors.json). */
  readonly errors?: ReadonlyMap<number, ErrorInfo>
  /** Called when this many requests in a row timed out: the link looks dead though nothing said so (default 3). */
  readonly stallAfter?: number
  readonly onStall?: () => void
  /** Called when a write failed: the link cannot carry requests, whether or not anything said it dropped. */
  readonly onSendFailed?: (error: unknown) => void
  readonly now?: () => number
}

interface Pending {
  readonly request: Request
  readonly options: CommandOptions
  readonly resolve: (data: Uint8Array) => void
  readonly reject: (error: Error) => void
  seq?: number
  sentAt?: number
  timer?: ReturnType<typeof setTimeout>
}

const hex = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(2, '0')}`
const SEQ_COUNT = 0x10000
/** At most this many requests on the wire, urgent ones included, so a seq is never taken twice. */
const WIRE_MAX = 0x100

export class CommandClient {
  private readonly defs: LinkDefinitions
  private readonly timeoutMs: number
  private readonly maxInFlight: number
  private readonly stallAfter: number
  private readonly now: () => number
  private readonly queue: Pending[] = []
  private readonly inFlight = new Map<number, Pending>()
  private readonly observers = new Set<(event: CommandEvent) => void>()
  private lastSeq = SEQ_COUNT - 1
  private timeouts = 0
  private closed = false

  constructor(private readonly options: CommandClientOptions) {
    this.defs = options.defs
    this.timeoutMs = options.timeoutMs ?? 2000
    this.maxInFlight = Math.max(1, Math.min(WIRE_MAX - 1, options.maxInFlight ?? 4))
    this.stallAfter = options.stallAfter ?? 3
    this.now = options.now ?? Date.now
  }

  /** The router's output for the request. */
  request(request: Request, options: CommandOptions = {}): Promise<Uint8Array> {
    if (this.closed) return Promise.reject(new CommandError('The command client is closed.', 'closed'))
    const max = this.defs.frameMax - this.defs.frameHead
    if (request.body.length > max) return Promise.reject(new CommandError(`A request of ${request.body.length} bytes is over the ${max} a frame holds.`, 'too-long'))
    return new Promise((resolve, reject) => {
      const entry: Pending = { request, options, resolve, reject }
      if (request.urgent) {
        const firstNormal = this.queue.findIndex((e) => !e.request.urgent)
        this.queue.splice(firstNormal < 0 ? this.queue.length : firstNormal, 0, entry)
      } else this.queue.push(entry)
      this.pump()
    })
  }

  observe(handler: (event: CommandEvent) => void): () => void {
    this.observers.add(handler)
    return () => this.observers.delete(handler)
  }

  get pending(): number {
    return this.queue.length + this.inFlight.size
  }

  /** An ANSWER frame from the board: it settles the request with its seq and key. */
  onAnswer(frame: Frame): void {
    const entry = this.inFlight.get(frame.seq)
    if (!entry || entry.request.key !== frame.key) {
      this.emit({ type: 'unmatched', seq: frame.seq, key: frame.key, at: this.now() })
      return
    }
    this.timeouts = 0
    const data = frame.data.slice()
    clearTimeout(entry.timer)
    this.inFlight.delete(frame.seq)
    this.emit({ type: 'answer', seq: frame.seq, ok: !frame.error, data, label: entry.options.label, sentAt: entry.sentAt ?? this.now(), at: this.now() })
    if (frame.error) entry.reject(this.refused(frame.seq, data, entry))
    else entry.resolve(data)
    this.pump()
  }

  /** Fails everything queued or waiting, the link being gone; the client stays usable. */
  cancelAll(code: Extract<CommandErrorCode, 'link-lost' | 'not-connected'>, message: string): void {
    const pending = [...this.inFlight.values(), ...this.queue.splice(0)]
    this.inFlight.clear()
    this.timeouts = 0
    for (const entry of pending) {
      clearTimeout(entry.timer)
      entry.reject(new CommandError(message, code, { seq: entry.seq }))
    }
  }

  close(): void {
    if (this.closed) return
    this.cancelAll('not-connected', 'The command client was closed.')
    this.closed = true
  }

  private pump(): void {
    while (!this.closed && this.queue.length > 0 && (this.inFlight.size < this.maxInFlight || (this.queue[0]!.request.urgent && this.inFlight.size < WIRE_MAX))) {
      const entry = this.queue.shift()!
      entry.seq = this.nextSeq()
      this.inFlight.set(entry.seq, entry)
      void this.write(entry)
    }
  }

  private nextSeq(): number {
    let seq = this.lastSeq
    do seq = (seq + 1) % SEQ_COUNT
    while (this.inFlight.has(seq))
    this.lastSeq = seq
    return seq
  }

  private async write(entry: Pending): Promise<void> {
    const seq = entry.seq!
    const frame = encodeRequest(this.defs, this.options.src, seq, entry.request)
    entry.sentAt = this.now()
    try {
      await this.options.send(frame)
    } catch (error) {
      if (this.inFlight.get(seq) !== entry) return // the link went, or the client closed, meanwhile: already failed
      this.inFlight.delete(seq)
      entry.reject(new CommandError(`Sending ${entry.options.label ?? `request ${hex(seq)}`} failed: ${error instanceof Error ? error.message : String(error)}`, 'send-failed', { seq, cause: error }))
      this.pump()
      this.options.onSendFailed?.(error)
      return
    }
    this.emit({ type: 'sent', seq, frame, label: entry.options.label, at: entry.sentAt })
    if (entry.request.noReply) {
      // never answered (a failure goes to the errors stream only): done once written, its seq free again
      if (this.inFlight.get(seq) === entry) this.inFlight.delete(seq)
      entry.resolve(new Uint8Array(0))
      this.pump()
      return
    }
    if (this.inFlight.get(seq) === entry) this.startTimer(seq, entry) // an answer can beat the write's own resolution
  }

  private startTimer(seq: number, entry: Pending): void {
    const wait = entry.options.timeoutMs ?? this.timeoutMs
    entry.timer = setTimeout(() => {
      if (this.inFlight.get(seq) !== entry) return
      this.inFlight.delete(seq)
      this.emit({ type: 'timeout', seq, label: entry.options.label, at: this.now() })
      entry.reject(new CommandError(`No answer to ${entry.options.label ?? `request ${hex(seq)}`} within ${wait} ms.`, 'timeout', { seq }))
      this.pump()
      if (++this.timeouts >= this.stallAfter) {
        this.timeouts = 0
        this.options.onStall?.()
      }
    }, wait)
  }

  private refused(seq: number, data: Uint8Array, entry: Pending): CommandError {
    const record = decodeErrorRecord(this.defs, data, this.options.errors)
    const cause = record?.cause
    const named = cause?.info ? `${cause.info.name}${cause.fields && Object.keys(cause.fields).length ? ` ${JSON.stringify(cause.fields)}` : ''}` : cause ? `tag ${hex(cause.tag)}` : 'no error record'
    return new CommandError(`The board refused ${entry.options.label ?? `request ${hex(seq)}`}: ${named}.`, 'device-error', { seq, ...(record ? { record } : {}) })
  }

  private emit(event: CommandEvent): void {
    this.observers.forEach((handler) => handler(event))
  }
}
