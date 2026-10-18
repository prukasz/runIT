import { refWithin } from '../errors/interpret'
import type { ErrorInterpreter, ErrorReport, Ref } from '../errors/interpret'
import { CLOCK_SET_AFTER_MS } from '../protocol/errors'
import type { ErrorRecord } from '../protocol/errors'
import { CommandError } from './commands'
import type { RunitLink } from './link'

/*
 * The errors of a board, kept for a user: what came on the errors stream (the board's error history: every reported chain,
 * internal ones too) and what came back as ERROR answers. The board does not suppress repeats and sends a command's error
 * both as the answer and on the stream (SYS_CONNECTOR.MD "Errors stream"), so:
 *
 *   - the same record twice (the same command: link, peer and seq, and the same chain) within `duplicateMs` is one;
 *   - the same error again (the same chain, `ErrorReport.key`) is the same entry: its count and time go up and it is unread
 *     again, it does not pile up as a new one;
 *   - what the user has read is marked (`markRead`) and removed with `clearRead`, or by reference (`clear({ ref })`) when the
 *     user fixed what it was about; past `capacity` the oldest read entries go first, then the oldest.
 *
 * An entry is dated by the board's clock in its record (UTC; a record from before the clock was set counts from the start of the board and
 * is dated once `setClock` knows the board's clock: the app reads it when the link comes up), else by when it arrived.
 *
 * Entries are found by the instances they are about (`entries({ ref })`: a device finds its pins' errors too).
 */

export interface ErrorEntry {
  /** Stable while the entry lives. */
  readonly id: number
  readonly report: ErrorReport
  /** How many times it happened. */
  readonly count: number
  readonly first: number
  readonly last: number
  readonly read: boolean
}

export interface ErrorFilter {
  /** Only unread (`false`) or only read (`true`) entries. */
  readonly read?: boolean
  /** Only entries about this instance or one inside it (`device:3` takes `device:3/pin:5`). */
  readonly ref?: Ref | string
  /** At least this severity. */
  readonly minSeverity?: number
  readonly ids?: readonly number[]
}

export interface ErrorLogOptions {
  /** Entries kept at most (default 200). */
  readonly capacity?: number
  /** The same record within this long is one (an answer and its copy on the stream; default 2000 ms). */
  readonly duplicateMs?: number
  readonly now?: () => number
}

interface Entry {
  id: number
  report: ErrorReport
  count: number
  first: number
  last: number
  read: boolean
  /** The last record that counted: its command header and when it came, for the duplicate check. */
  lastCommand: string | undefined
}

const commandKey = (record: ErrorRecord): string | undefined =>
  record.command && `${record.command.link}:${record.command.peer}:${record.command.seq}`

export class ErrorLog {
  private readonly items = new Map<string, Entry>()
  private readonly handlers = new Set<(entries: readonly ErrorEntry[]) => void>()
  private readonly detach: (() => void)[] = []
  private nextId = 1
  private readonly capacity: number
  private readonly duplicateMs: number
  private readonly now: () => number
  /** The board's clock minus its uptime (ms): what dates a time since the start. Unknown until `setClock`. */
  private offset: number | undefined

  constructor(
    readonly interpreter: ErrorInterpreter,
    options: ErrorLogOptions = {},
  ) {
    this.capacity = Math.max(1, options.capacity ?? 200)
    this.duplicateMs = options.duplicateMs ?? 2000
    this.now = options.now ?? Date.now
  }

  /** Takes the link's errors stream; returns a function that stops it. */
  attach(link: RunitLink): () => void {
    const off = link.on('errors', (data) => void this.addBytes(data))
    this.detach.push(off)
    return off
  }

  /** The board's clock now (`unixMs` and `uptimeMs` of the same moment): a record stamped since the start is dated from it. */
  setClock(unixMs: number, uptimeMs: number): void {
    this.offset = unixMs - uptimeMs
  }

  /** When a record happened: its stamp as UTC, or the arrival time while the stamp cannot be dated. */
  private dated(report: ErrorReport): number {
    const stamp = report.record.time
    if (stamp >= CLOCK_SET_AFTER_MS) return stamp
    return this.offset === undefined ? this.now() : stamp + this.offset
  }

  /** A record's bytes (the errors stream, or an ERROR answer's data). Undefined when they are not a record. */
  addBytes(data: Uint8Array): ErrorEntry | undefined {
    const report = this.interpreter.read(data)
    return report && this.add(report)
  }

  /** The record of a failed command (`CommandError` with code `device-error`); other failures are not the board's errors. */
  addCommandError(error: unknown): ErrorEntry | undefined {
    return error instanceof CommandError && error.record ? this.add(this.interpreter.interpret(error.record)) : undefined
  }

  add(report: ErrorReport): ErrorEntry {
    const at = this.dated(report)
    const command = commandKey(report.record)
    const known = this.items.get(report.key)
    if (known) {
      // Not new: the copy of a command's error within `duplicateMs`, or a record the entry already counted (the board's history read again).
      const duplicate = at <= known.last || (command !== undefined && command === known.lastCommand && at - known.last <= this.duplicateMs)
      if (!duplicate) {
        known.count++
        known.read = false
      }
      known.last = Math.max(known.last, at)
      known.report = report
      known.lastCommand = command
      this.changed()
      return view(known)
    }
    const entry: Entry = { id: this.nextId++, report, count: 1, first: at, last: at, read: false, lastCommand: command }
    this.items.set(report.key, entry)
    this.trim()
    this.changed()
    return view(entry)
  }

  /** Newest first. */
  entries(filter: ErrorFilter = {}): ErrorEntry[] {
    return [...this.items.values()]
      .filter((e) => matches(e, filter))
      .sort((a, b) => b.last - a.last || b.id - a.id)
      .map(view)
  }

  /** How many unread entries there are (about `ref`, when given). */
  unread(ref?: Ref | string): number {
    return this.entries({ read: false, ...(ref ? { ref } : {}) }).length
  }

  /** Marks the matching entries read (every entry when no filter is given). */
  markRead(filter: ErrorFilter = {}): number {
    let n = 0
    for (const e of this.items.values()) {
      if (!e.read && matches(e, filter)) {
        e.read = true
        n++
      }
    }
    if (n > 0) this.changed()
    return n
  }

  /** Removes the entries the user has read. */
  clearRead(): number {
    return this.clear({ read: true })
  }

  /** Removes the matching entries (every entry when no filter is given). */
  clear(filter: ErrorFilter = {}): number {
    let n = 0
    for (const [key, e] of this.items) {
      if (matches(e, filter)) {
        this.items.delete(key)
        n++
      }
    }
    if (n > 0) this.changed()
    return n
  }

  onChange(handler: (entries: readonly ErrorEntry[]) => void): () => void {
    this.handlers.add(handler)
    return () => this.handlers.delete(handler)
  }

  /** Stops taking the link's errors stream. */
  close(): void {
    for (const off of this.detach.splice(0)) off()
  }

  private trim(): void {
    while (this.items.size > this.capacity) {
      const all = [...this.items.entries()].sort(([, a], [, b]) => Number(b.read) - Number(a.read) || a.last - b.last)
      this.items.delete(all[0]![0])
    }
  }

  private changed(): void {
    const entries = this.entries()
    for (const handler of this.handlers) handler(entries)
  }
}

const view = (e: Entry): ErrorEntry => ({ id: e.id, report: e.report, count: e.count, first: e.first, last: e.last, read: e.read })

const matches = (e: Entry, filter: ErrorFilter): boolean =>
  (filter.read === undefined || e.read === filter.read) &&
  (filter.minSeverity === undefined || e.report.severity >= filter.minSeverity) &&
  (filter.ids === undefined || filter.ids.includes(e.id)) &&
  (filter.ref === undefined || e.report.refs.some((r) => refWithin(r, filter.ref!)))

