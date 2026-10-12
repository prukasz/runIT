import { ErrorCatalog } from './errors'
import { buildRequest, decodeErrorRecord, parseResponse, STATUS_ERROR, STATUS_OK, STREAM } from './frames'
import type { ErrorRecord } from './frames'

/** What the client needs of a link: whole frames in and out (the BLE link, a UART link, a test double). */
export interface FrameLink {
  send(frame: Uint8Array): Promise<void>
  /** Every frame the board sends, first byte = stream byte. */
  onFrame(handler: (stream: 'main' | 'logs', frame: Uint8Array) => void): () => void
}

/** The board answered with an error record. */
export class CommandError extends Error {
  readonly record: ErrorRecord | undefined
  /** The tag name of the root cause (`ERR_VM_OBJ_BAD_TYPE`), when the record could be read. */
  readonly cause_: string | undefined

  constructor(message: string, record: ErrorRecord | undefined, rootCause: string | undefined) {
    super(message)
    this.name = 'CommandError'
    this.record = record
    this.cause_ = rootCause
  }
}

/** No answer within the time limit (the board is stuck, the link is half dead, or the request never arrived). */
export class CommandTimeout extends Error {
  readonly seq: number
  readonly ms: number

  constructor(seq: number, ms: number) {
    super(`No answer to request ${seq} in ${ms} ms.`)
    this.name = 'CommandTimeout'
    this.seq = seq
    this.ms = ms
  }
}

export class LinkClosed extends Error {
  constructor(reason = 'The link closed.') {
    super(reason)
    this.name = 'LinkClosed'
  }
}

export interface RequestOptions {
  /** The app decides what runs on the board's urgent task (a STOP, a write that must not wait). */
  readonly urgent?: boolean
  readonly timeoutMs?: number
}

interface Pending {
  readonly parts: Uint8Array[]
  readonly count: number
  readonly resolve: (parts: Uint8Array[]) => void
  readonly reject: (error: Error) => void
  readonly timer: ReturnType<typeof setTimeout>
}

type Handler<T> = (value: T) => void

/** Sequence numbers 1..SEQ_LAST are the client's; the ping monitor and tools use the numbers above. */
export const SEQ_LAST = 199

/**
 * Commands and streams over one link: requests are matched to answers by `seq` (answers can come out of order),
 * an error answer becomes a `CommandError` with the decoded chain, and the board's streams are routed to handlers
 * (telemetry, logs, errors, events).
 */
export class RouterClient {
  private readonly pending = new Map<number, Pending>()
  private seq = 0
  private readonly telemetry = new Set<Handler<Uint8Array>>()
  private readonly logs = new Set<Handler<string>>()
  private readonly errors = new Set<Handler<ErrorRecord>>()
  private readonly events = new Set<Handler<Uint8Array>>()
  private readonly stop: () => void
  private readonly link: FrameLink
  private readonly timeoutMs: number
  readonly catalog: ErrorCatalog

  constructor(link: FrameLink, options: { readonly timeoutMs?: number; readonly catalog?: ErrorCatalog } = {}) {
    this.link = link
    this.timeoutMs = options.timeoutMs ?? 2000
    this.catalog = options.catalog ?? new ErrorCatalog()
    this.stop = link.onFrame((_, frame) => this.receive(frame))
  }

  onTelemetry(handler: Handler<Uint8Array>): () => void {
    this.telemetry.add(handler)
    return () => this.telemetry.delete(handler)
  }

  /** Log lines (the text the board's ESP_LOG prints, one per frame). */
  onLog(handler: Handler<string>): () => void {
    this.logs.add(handler)
    return () => this.logs.delete(handler)
  }

  /** Every error the board reports, also those of other clients' commands and of the board itself. */
  onError(handler: Handler<ErrorRecord>): () => void {
    this.errors.add(handler)
    return () => this.errors.delete(handler)
  }

  /** Raw events stream frames (`[0x01 NOTIFY][sys_event_t]`, SYS_EVENT.MD). */
  onEvent(handler: Handler<Uint8Array>): () => void {
    this.events.add(handler)
    return () => this.events.delete(handler)
  }

  /** One request, one answer: the answer's data. Rejects with CommandError, CommandTimeout or LinkClosed. */
  async request(keys: readonly number[], payload?: Uint8Array, options: RequestOptions = {}): Promise<Uint8Array> {
    const [data] = await this.send(keys, payload, 1, options)
    return data
  }

  /** A command that answers in `count` packets with one seq (actions READ): the data of each, in order. An error answer ends it. */
  requestMany(keys: readonly number[], payload: Uint8Array | undefined, count: number, options: RequestOptions = {}): Promise<Uint8Array[]> {
    return this.send(keys, payload, count, options)
  }

  /** Cancels what waits for an answer and stops listening. The link stays up. */
  close(reason?: string): void {
    this.stop()
    for (const [seq, entry] of this.pending) {
      clearTimeout(entry.timer)
      entry.reject(new LinkClosed(reason))
      this.pending.delete(seq)
    }
  }

  private nextSeq(): number {
    for (let i = 0; i < SEQ_LAST; i++) {
      this.seq = this.seq >= SEQ_LAST ? 1 : this.seq + 1
      if (!this.pending.has(this.seq)) return this.seq
    }
    throw new Error('Too many requests are waiting for an answer.')
  }

  private send(keys: readonly number[], payload: Uint8Array | undefined, count: number, options: RequestOptions): Promise<Uint8Array[]> {
    const seq = this.nextSeq()
    const frame = buildRequest(seq, keys, payload, options.urgent)
    const ms = options.timeoutMs ?? this.timeoutMs
    return new Promise<Uint8Array[]>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(seq)
        reject(new CommandTimeout(seq, ms))
      }, ms)
      this.pending.set(seq, { parts: [], count, resolve, reject, timer })
      this.link.send(frame).catch((error: unknown) => {
        clearTimeout(timer)
        this.pending.delete(seq)
        reject(error instanceof Error ? error : new Error(String(error)))
      })
    })
  }

  private receive(frame: Uint8Array): void {
    switch (frame[0]) {
      case STREAM.response:
        return this.answer(frame)
      case STREAM.telemetry:
        return this.telemetry.forEach((handler) => handler(frame.subarray(1)))
      case STREAM.logs:
        return this.logs.forEach((handler) => handler(new TextDecoder().decode(frame.subarray(1)).replace(/\r?\n$/, '')))
      case STREAM.errors:
        try {
          const record = decodeErrorRecord(frame.subarray(1))
          this.errors.forEach((handler) => handler(record))
        } catch {
          // a record cut short by a small link: nothing to show
        }
        return
      case STREAM.events:
        return this.events.forEach((handler) => handler(frame.subarray(1)))
    }
  }

  private answer(frame: Uint8Array): void {
    const response = parseResponse(frame)
    const entry = response && this.pending.get(response.seq)
    if (!response || !entry) return // not ours (another client's, or a late answer to a request that timed out)
    if (response.status === STATUS_ERROR) {
      this.finish(response.seq, entry, undefined, this.failure(response.data))
    } else if (response.status === STATUS_OK) {
      entry.parts.push(response.data)
      if (entry.parts.length >= entry.count) this.finish(response.seq, entry, entry.parts)
    } else {
      this.finish(response.seq, entry, undefined, new Error(`Unknown answer status ${response.status}.`))
    }
  }

  private finish(seq: number, entry: Pending, parts?: Uint8Array[], error?: Error): void {
    clearTimeout(entry.timer)
    this.pending.delete(seq)
    if (error) entry.reject(error)
    else entry.resolve(parts ?? [])
  }

  private failure(data: Uint8Array): CommandError {
    try {
      const record = decodeErrorRecord(data)
      return new CommandError(this.catalog.format(record) || 'The board refused the command.', record, this.catalog.rootCause(record))
    } catch {
      return new CommandError('The board refused the command (its error record could not be read).', undefined, undefined)
    }
  }
}
