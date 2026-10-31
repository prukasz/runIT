import type { ErrorInfo, LinkDefinitions } from '../definitions'
import { decodeErrorRecord } from '../protocol/errors'
import type { ErrorRecord } from '../protocol/errors'
import { decodeResponse, encodeRequest } from '../protocol/frames'
import type { Request } from '../protocol/frames'

/*
 * Commands over any link: a request gets a sequence byte, the answer is matched by it (answers may come out of order: urgent
 * frames overtake), a command that gets no answer times out. Several commands can be on the wire at once (`maxInFlight`): the
 * board runs normal commands in the order they arrived, so pipelining saves one round trip per command without reordering
 * anything. An urgent command goes before every normal one still queued and is written even when the window is full: STOP and
 * the dead-man's heartbeat must not wait behind commands the board is slow to answer. The sequence counts on instead of reusing
 * the lowest free byte, so a late answer cannot match a newer command.
 */

export type CommandErrorCode =
  /** No answer in time. */
  | 'timeout'
  /** The link dropped while the command was queued or on the wire: it may or may not have run. */
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
  /** Shown in the observer events. */
  readonly label?: string
}

export type CommandEvent =
  | { readonly type: 'sent'; readonly seq: number; readonly frame: Uint8Array; readonly label?: string; readonly at: number }
  | { readonly type: 'answer'; readonly seq: number; readonly ok: boolean; readonly data: Uint8Array; readonly label?: string; readonly sentAt: number; readonly at: number }
  | { readonly type: 'timeout'; readonly seq: number; readonly label?: string; readonly at: number }
  /** An answer nobody waits for (late after a timeout, or to a command of another session). */
  | { readonly type: 'unmatched'; readonly seq: number; readonly at: number }

export interface CommandClientOptions {
  readonly defs: LinkDefinitions
  /** Puts one whole frame on the link; rejects when it could not. */
  readonly send: (frame: Uint8Array) => Promise<void>
  /** Default wait for an answer (default 2000 ms). */
  readonly timeoutMs?: number
  /** Commands on the wire at once (default 4). */
  readonly maxInFlight?: number
  /** Names the errors of an ERROR answer (errors.json). */
  readonly errors?: ReadonlyMap<number, ErrorInfo>
  /** Called when this many commands in a row timed out: the link looks dead though nothing said so (default 3). */
  readonly stallAfter?: number
  readonly onStall?: () => void
  /**
   * Called when a write failed. A write is acknowledged, so a failure means the link cannot carry requests whether or not anything
   * said it dropped (after a GATT change the OS invalidates the handles and reports no disconnect).
   */
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

export class CommandClient {
  private readonly defs: LinkDefinitions
  private readonly send: (frame: Uint8Array) => Promise<void>
  private readonly timeoutMs: number
  private readonly maxInFlight: number
  private readonly stallAfter: number
  private readonly now: () => number
  private readonly queue: Pending[] = []
  private readonly inFlight = new Map<number, Pending>()
  private readonly observers = new Set<(event: CommandEvent) => void>()
  private lastSeq = 0xff
  private timeouts = 0
  private closed = false

  constructor(private readonly options: CommandClientOptions) {
    this.defs = options.defs
    this.send = options.send
    this.timeoutMs = options.timeoutMs ?? 2000
    this.maxInFlight = Math.max(1, Math.min(255, options.maxInFlight ?? 4))
    this.stallAfter = options.stallAfter ?? 3
    this.now = options.now ?? Date.now
  }

  /** The router's output for the request. */
  request(request: Request, options: CommandOptions = {}): Promise<Uint8Array> {
    if (this.closed) return Promise.reject(new CommandError('The command client is closed.', 'closed'))
    const length = request.keys.length + (request.payload?.length ?? 0)
    const max = this.defs.frameMax - this.defs.frameHead
    if (length > max) return Promise.reject(new CommandError(`A request of ${length} bytes is over the ${max} the board takes.`, 'too-long'))
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

  /** A frame of the response stream. Frames of other streams are not for the client. */
  onFrame(frame: Uint8Array): void {
    const response = decodeResponse(this.defs, frame)
    if (!response) return
    const entry = this.inFlight.get(response.seq)
    if (!entry) {
      this.emit({ type: 'unmatched', seq: response.seq, at: this.now() })
      return
    }
    this.timeouts = 0
    const data = response.data.slice()
    clearTimeout(entry.timer)
    this.inFlight.delete(response.seq)
    this.emit({ type: 'answer', seq: response.seq, ok: response.ok, data, label: entry.options.label, sentAt: entry.sentAt ?? this.now(), at: this.now() })
    if (response.ok) entry.resolve(data)
    else entry.reject(this.deviceError(response.seq, data, entry))
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
    this.closed = true
    const pending = [...this.inFlight.values(), ...this.queue.splice(0)]
    this.inFlight.clear()
    for (const entry of pending) {
      clearTimeout(entry.timer)
      entry.reject(new CommandError('The command client was closed.', 'closed', { seq: entry.seq }))
    }
  }

  private pump(): void {
    // an urgent command passes a full window, never the 256 sequence bytes
    while (!this.closed && this.queue.length > 0 && (this.inFlight.size < this.maxInFlight || (this.queue[0]!.request.urgent && this.inFlight.size < 0x100))) {
      const entry = this.queue.shift()!
      entry.seq = this.nextSeq()
      this.inFlight.set(entry.seq, entry)
      void this.write(entry)
    }
  }

  private nextSeq(): number {
    for (let step = 1; step <= 0x100; step++) {
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
    const frame = encodeRequest(this.defs, seq, entry.request)
    entry.sentAt = this.now()
    try {
      await this.send(frame)
    } catch (error) {
      if (this.inFlight.get(seq) !== entry) return // the link went, or the client closed, meanwhile: already failed
      this.inFlight.delete(seq)
      entry.reject(new CommandError(`Sending command ${hex(seq)} failed: ${error instanceof Error ? error.message : String(error)}`, 'send-failed', { seq, cause: error }))
      this.pump()
      this.options.onSendFailed?.(error)
      return
    }
    this.emit({ type: 'sent', seq, frame, label: entry.options.label, at: entry.sentAt })
    if (this.inFlight.get(seq) === entry) this.restartTimer(seq, entry) // not answered yet (an answer can beat the write's own resolution)
  }

  private restartTimer(seq: number, entry: Pending): void {
    clearTimeout(entry.timer)
    const wait = entry.options.timeoutMs ?? this.timeoutMs
    entry.timer = setTimeout(() => {
      if (this.inFlight.get(seq) !== entry) return
      this.inFlight.delete(seq)
      this.emit({ type: 'timeout', seq, label: entry.options.label, at: this.now() })
      entry.reject(new CommandError(`No answer to command ${hex(seq)} within ${wait} ms.`, 'timeout', { seq }))
      this.pump()
      if (++this.timeouts >= this.stallAfter) {
        this.timeouts = 0
        this.options.onStall?.()
      }
    }, wait)
  }

  private deviceError(seq: number, data: Uint8Array, entry: Pending): CommandError {
    const record = decodeErrorRecord(this.defs, data, this.options.errors)
    const cause = record?.cause
    const named = cause?.info ? `${cause.info.name}${cause.fields && Object.keys(cause.fields).length ? ` ${JSON.stringify(cause.fields)}` : ''}` : cause ? `tag ${hex(cause.tag)}` : 'no error record'
    return new CommandError(`The board refused command ${entry.options.label ?? hex(seq)}: ${named}.`, 'device-error', { seq, ...(record ? { record } : {}) })
  }

  private emit(event: CommandEvent): void {
    this.observers.forEach((handler) => handler(event))
  }
}
